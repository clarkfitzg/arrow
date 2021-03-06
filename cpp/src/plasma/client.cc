// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

// PLASMA CLIENT: Client library for using the plasma store and manager

#include "plasma/client.h"

#ifdef _WIN32
#include <Win32_Interop/win32_types.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <thread>
#include <vector>

#include "plasma/common.h"
#include "plasma/fling.h"
#include "plasma/io.h"
#include "plasma/plasma.h"
#include "plasma/protocol.h"

#define XXH_STATIC_LINKING_ONLY
#include "thirdparty/xxhash.h"

#define XXH64_DEFAULT_SEED 0

namespace plasma {

// Number of threads used for memcopy and hash computations.
constexpr int64_t kThreadPoolSize = 8;
constexpr int64_t kBytesInMB = 1 << 20;
static std::vector<std::thread> threadpool_(kThreadPoolSize);

struct ObjectInUseEntry {
  /// A count of the number of times this client has called PlasmaClient::Create
  /// or
  /// PlasmaClient::Get on this object ID minus the number of calls to
  /// PlasmaClient::Release.
  /// When this count reaches zero, we remove the entry from the ObjectsInUse
  /// and decrement a count in the relevant ClientMmapTableEntry.
  int count;
  /// Cached information to read the object.
  PlasmaObject object;
  /// A flag representing whether the object has been sealed.
  bool is_sealed;
};

PlasmaClient::PlasmaClient() {}

PlasmaClient::~PlasmaClient() {}

// If the file descriptor fd has been mmapped in this client process before,
// return the pointer that was returned by mmap, otherwise mmap it and store the
// pointer in a hash table.
uint8_t* PlasmaClient::lookup_or_mmap(int fd, int store_fd_val, int64_t map_size) {
  auto entry = mmap_table_.find(store_fd_val);
  if (entry != mmap_table_.end()) {
    close(fd);
    return entry->second.pointer;
  } else {
    uint8_t* result = reinterpret_cast<uint8_t*>(
        mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    // TODO(pcm): Don't fail here, instead return a Status.
    if (result == MAP_FAILED) { ARROW_LOG(FATAL) << "mmap failed"; }
    close(fd);
    ClientMmapTableEntry& entry = mmap_table_[store_fd_val];
    entry.pointer = result;
    entry.length = map_size;
    entry.count = 0;
    return result;
  }
}

// Get a pointer to a file that we know has been memory mapped in this client
// process before.
uint8_t* PlasmaClient::lookup_mmapped_file(int store_fd_val) {
  auto entry = mmap_table_.find(store_fd_val);
  ARROW_CHECK(entry != mmap_table_.end());
  return entry->second.pointer;
}

void PlasmaClient::increment_object_count(
    const ObjectID& object_id, PlasmaObject* object, bool is_sealed) {
  // Increment the count of the object to track the fact that it is being used.
  // The corresponding decrement should happen in PlasmaClient::Release.
  auto elem = objects_in_use_.find(object_id);
  ObjectInUseEntry* object_entry;
  if (elem == objects_in_use_.end()) {
    // Add this object ID to the hash table of object IDs in use. The
    // corresponding call to free happens in PlasmaClient::Release.
    objects_in_use_[object_id] =
        std::unique_ptr<ObjectInUseEntry>(new ObjectInUseEntry());
    objects_in_use_[object_id]->object = *object;
    objects_in_use_[object_id]->count = 0;
    objects_in_use_[object_id]->is_sealed = is_sealed;
    object_entry = objects_in_use_[object_id].get();
    // Increment the count of the number of objects in the memory-mapped file
    // that are being used. The corresponding decrement should happen in
    // PlasmaClient::Release.
    auto entry = mmap_table_.find(object->handle.store_fd);
    ARROW_CHECK(entry != mmap_table_.end());
    ARROW_CHECK(entry->second.count >= 0);
    // Update the in_use_object_bytes_.
    in_use_object_bytes_ +=
        (object_entry->object.data_size + object_entry->object.metadata_size);
    entry->second.count += 1;
  } else {
    object_entry = elem->second.get();
    ARROW_CHECK(object_entry->count > 0);
  }
  // Increment the count of the number of instances of this object that are
  // being used by this client. The corresponding decrement should happen in
  // PlasmaClient::Release.
  object_entry->count += 1;
}

Status PlasmaClient::Create(const ObjectID& object_id, int64_t data_size,
    uint8_t* metadata, int64_t metadata_size, uint8_t** data) {
  ARROW_LOG(DEBUG) << "called plasma_create on conn " << store_conn_ << " with size "
                   << data_size << " and metadata size " << metadata_size;
  RETURN_NOT_OK(SendCreateRequest(store_conn_, object_id, data_size, metadata_size));
  std::vector<uint8_t> buffer;
  RETURN_NOT_OK(PlasmaReceive(store_conn_, MessageType_PlasmaCreateReply, &buffer));
  ObjectID id;
  PlasmaObject object;
  RETURN_NOT_OK(ReadCreateReply(buffer.data(), &id, &object));
  // If the CreateReply included an error, then the store will not send a file
  // descriptor.
  int fd = recv_fd(store_conn_);
  ARROW_CHECK(fd >= 0) << "recv not successful";
  ARROW_CHECK(object.data_size == data_size);
  ARROW_CHECK(object.metadata_size == metadata_size);
  // The metadata should come right after the data.
  ARROW_CHECK(object.metadata_offset == object.data_offset + data_size);
  *data = lookup_or_mmap(fd, object.handle.store_fd, object.handle.mmap_size) +
          object.data_offset;
  // If plasma_create is being called from a transfer, then we will not copy the
  // metadata here. The metadata will be written along with the data streamed
  // from the transfer.
  if (metadata != NULL) {
    // Copy the metadata to the buffer.
    memcpy(*data + object.data_size, metadata, metadata_size);
  }
  // Increment the count of the number of instances of this object that this
  // client is using. A call to PlasmaClient::Release is required to decrement
  // this
  // count. Cache the reference to the object.
  increment_object_count(object_id, &object, false);
  // We increment the count a second time (and the corresponding decrement will
  // happen in a PlasmaClient::Release call in plasma_seal) so even if the
  // buffer
  // returned by PlasmaClient::Dreate goes out of scope, the object does not get
  // released before the call to PlasmaClient::Seal happens.
  increment_object_count(object_id, &object, false);
  return Status::OK();
}

Status PlasmaClient::Get(const ObjectID* object_ids, int64_t num_objects,
    int64_t timeout_ms, ObjectBuffer* object_buffers) {
  // Fill out the info for the objects that are already in use locally.
  bool all_present = true;
  for (int i = 0; i < num_objects; ++i) {
    auto object_entry = objects_in_use_.find(object_ids[i]);
    if (object_entry == objects_in_use_.end()) {
      // This object is not currently in use by this client, so we need to send
      // a request to the store.
      all_present = false;
      // Make a note to ourselves that the object is not present.
      object_buffers[i].data_size = -1;
    } else {
      // NOTE: If the object is still unsealed, we will deadlock, since we must
      // have been the one who created it.
      ARROW_CHECK(object_entry->second->is_sealed)
          << "Plasma client called get on an unsealed object that it created";
      PlasmaObject* object = &object_entry->second->object;
      object_buffers[i].data = lookup_mmapped_file(object->handle.store_fd);
      object_buffers[i].data = object_buffers[i].data + object->data_offset;
      object_buffers[i].data_size = object->data_size;
      object_buffers[i].metadata = object_buffers[i].data + object->data_size;
      object_buffers[i].metadata_size = object->metadata_size;
      // Increment the count of the number of instances of this object that this
      // client is using. A call to PlasmaClient::Release is required to
      // decrement this
      // count. Cache the reference to the object.
      increment_object_count(object_ids[i], object, true);
    }
  }

  if (all_present) { return Status::OK(); }

  // If we get here, then the objects aren't all currently in use by this
  // client, so we need to send a request to the plasma store.
  RETURN_NOT_OK(SendGetRequest(store_conn_, object_ids, num_objects, timeout_ms));
  std::vector<uint8_t> buffer;
  RETURN_NOT_OK(PlasmaReceive(store_conn_, MessageType_PlasmaGetReply, &buffer));
  std::vector<ObjectID> received_object_ids(num_objects);
  std::vector<PlasmaObject> object_data(num_objects);
  PlasmaObject* object;
  RETURN_NOT_OK(ReadGetReply(
      buffer.data(), received_object_ids.data(), object_data.data(), num_objects));

  for (int i = 0; i < num_objects; ++i) {
    DCHECK(received_object_ids[i] == object_ids[i]);
    object = &object_data[i];
    if (object_buffers[i].data_size != -1) {
      // If the object was already in use by the client, then the store should
      // have returned it.
      DCHECK_NE(object->data_size, -1);
      // We won't use this file descriptor, but the store sent us one, so we
      // need to receive it and then close it right away so we don't leak file
      // descriptors.
      int fd = recv_fd(store_conn_);
      close(fd);
      ARROW_CHECK(fd >= 0);
      // We've already filled out the information for this object, so we can
      // just continue.
      continue;
    }
    // If we are here, the object was not currently in use, so we need to
    // process the reply from the object store.
    if (object->data_size != -1) {
      // The object was retrieved. The user will be responsible for releasing
      // this object.
      int fd = recv_fd(store_conn_);
      ARROW_CHECK(fd >= 0);
      object_buffers[i].data =
          lookup_or_mmap(fd, object->handle.store_fd, object->handle.mmap_size);
      // Finish filling out the return values.
      object_buffers[i].data = object_buffers[i].data + object->data_offset;
      object_buffers[i].data_size = object->data_size;
      object_buffers[i].metadata = object_buffers[i].data + object->data_size;
      object_buffers[i].metadata_size = object->metadata_size;
      // Increment the count of the number of instances of this object that this
      // client is using. A call to PlasmaClient::Release is required to
      // decrement this
      // count. Cache the reference to the object.
      increment_object_count(received_object_ids[i], object, true);
    } else {
      // The object was not retrieved. Make sure we already put a -1 here to
      // indicate that the object was not retrieved. The caller is not
      // responsible for releasing this object.
      DCHECK_EQ(object_buffers[i].data_size, -1);
      object_buffers[i].data_size = -1;
    }
  }
  return Status::OK();
}

/// This is a helper method for implementing plasma_release. We maintain a
/// buffer
/// of release calls and only perform them once the buffer becomes full (as
/// judged by the aggregate sizes of the objects). There may be multiple release
/// calls for the same object ID in the buffer. In this case, the first release
/// calls will not do anything. The client will only send a message to the store
/// releasing the object when the client is truly done with the object.
///
/// @param conn The plasma connection.
/// @param object_id The object ID to attempt to release.
Status PlasmaClient::PerformRelease(const ObjectID& object_id) {
  // Decrement the count of the number of instances of this object that are
  // being used by this client. The corresponding increment should have happened
  // in PlasmaClient::Get.
  auto object_entry = objects_in_use_.find(object_id);
  ARROW_CHECK(object_entry != objects_in_use_.end());
  object_entry->second->count -= 1;
  ARROW_CHECK(object_entry->second->count >= 0);
  // Check if the client is no longer using this object.
  if (object_entry->second->count == 0) {
    // Decrement the count of the number of objects in this memory-mapped file
    // that the client is using. The corresponding increment should have
    // happened in plasma_get.
    int fd = object_entry->second->object.handle.store_fd;
    auto entry = mmap_table_.find(fd);
    ARROW_CHECK(entry != mmap_table_.end());
    entry->second.count -= 1;
    ARROW_CHECK(entry->second.count >= 0);
    // If none are being used then unmap the file.
    if (entry->second.count == 0) {
      munmap(entry->second.pointer, entry->second.length);
      // Remove the corresponding entry from the hash table.
      mmap_table_.erase(fd);
    }
    // Tell the store that the client no longer needs the object.
    RETURN_NOT_OK(SendReleaseRequest(store_conn_, object_id));
    // Update the in_use_object_bytes_.
    in_use_object_bytes_ -= (object_entry->second->object.data_size +
                             object_entry->second->object.metadata_size);
    DCHECK_GE(in_use_object_bytes_, 0);
    // Remove the entry from the hash table of objects currently in use.
    objects_in_use_.erase(object_id);
  }
  return Status::OK();
}

Status PlasmaClient::Release(const ObjectID& object_id) {
  // If the client is already disconnected, ignore release requests.
  if (store_conn_ < 0) {
    return Status::OK();
  }
  // Add the new object to the release history.
  release_history_.push_front(object_id);
  // If there are too many bytes in use by the client or if there are too many
  // pending release calls, and there are at least some pending release calls in
  // the release_history list, then release some objects.
  while ((in_use_object_bytes_ > std::min(kL3CacheSizeBytes, store_capacity_ / 100) ||
             release_history_.size() > config_.release_delay) &&
         release_history_.size() > 0) {
    // Perform a release for the object ID for the first pending release.
    RETURN_NOT_OK(PerformRelease(release_history_.back()));
    // Remove the last entry from the release history.
    release_history_.pop_back();
  }
  return Status::OK();
}

// This method is used to query whether the plasma store contains an object.
Status PlasmaClient::Contains(const ObjectID& object_id, bool* has_object) {
  // Check if we already have a reference to the object.
  if (objects_in_use_.count(object_id) > 0) {
    *has_object = 1;
  } else {
    // If we don't already have a reference to the object, check with the store
    // to see if we have the object.
    RETURN_NOT_OK(SendContainsRequest(store_conn_, object_id));
    std::vector<uint8_t> buffer;
    RETURN_NOT_OK(PlasmaReceive(store_conn_, MessageType_PlasmaContainsReply, &buffer));
    ObjectID object_id2;
    RETURN_NOT_OK(ReadContainsReply(buffer.data(), &object_id2, has_object));
  }
  return Status::OK();
}

static void ComputeBlockHash(const unsigned char* data, int64_t nbytes, uint64_t* hash) {
  XXH64_state_t hash_state;
  XXH64_reset(&hash_state, XXH64_DEFAULT_SEED);
  XXH64_update(&hash_state, data, nbytes);
  *hash = XXH64_digest(&hash_state);
}

static inline bool compute_object_hash_parallel(
    XXH64_state_t* hash_state, const unsigned char* data, int64_t nbytes) {
  // Note that this function will likely be faster if the address of data is
  // aligned on a 64-byte boundary.
  const int num_threads = kThreadPoolSize;
  uint64_t threadhash[num_threads + 1];
  const uint64_t data_address = reinterpret_cast<uint64_t>(data);
  const uint64_t num_blocks = nbytes / BLOCK_SIZE;
  const uint64_t chunk_size = (num_blocks / num_threads) * BLOCK_SIZE;
  const uint64_t right_address = data_address + chunk_size * num_threads;
  const uint64_t suffix = (data_address + nbytes) - right_address;
  // Now the data layout is | k * num_threads * block_size | suffix | ==
  // | num_threads * chunk_size | suffix |, where chunk_size = k * block_size.
  // Each thread gets a "chunk" of k blocks, except the suffix thread.

  for (int i = 0; i < num_threads; i++) {
    threadpool_[i] = std::thread(ComputeBlockHash,
        reinterpret_cast<uint8_t*>(data_address) + i * chunk_size, chunk_size,
        &threadhash[i]);
  }
  ComputeBlockHash(
      reinterpret_cast<uint8_t*>(right_address), suffix, &threadhash[num_threads]);

  // Join the threads.
  for (auto& t : threadpool_) {
    if (t.joinable()) { t.join(); }
  }

  XXH64_update(hash_state, (unsigned char*)threadhash, sizeof(threadhash));
  return true;
}

static uint64_t compute_object_hash(const ObjectBuffer& obj_buffer) {
  XXH64_state_t hash_state;
  XXH64_reset(&hash_state, XXH64_DEFAULT_SEED);
  if (obj_buffer.data_size >= kBytesInMB) {
    compute_object_hash_parallel(
        &hash_state, (unsigned char*)obj_buffer.data, obj_buffer.data_size);
  } else {
    XXH64_update(&hash_state, (unsigned char*)obj_buffer.data, obj_buffer.data_size);
  }
  XXH64_update(
      &hash_state, (unsigned char*)obj_buffer.metadata, obj_buffer.metadata_size);
  return XXH64_digest(&hash_state);
}

Status PlasmaClient::Seal(const ObjectID& object_id) {
  // Make sure this client has a reference to the object before sending the
  // request to Plasma.
  auto object_entry = objects_in_use_.find(object_id);
  ARROW_CHECK(object_entry != objects_in_use_.end())
      << "Plasma client called seal an object without a reference to it";
  ARROW_CHECK(!object_entry->second->is_sealed)
      << "Plasma client called seal an already sealed object";
  object_entry->second->is_sealed = true;
  /// Send the seal request to Plasma.
  static unsigned char digest[kDigestSize];
  RETURN_NOT_OK(Hash(object_id, &digest[0]));
  RETURN_NOT_OK(SendSealRequest(store_conn_, object_id, &digest[0]));
  // We call PlasmaClient::Release to decrement the number of instances of this
  // object
  // that are currently being used by this client. The corresponding increment
  // happened in plasma_create and was used to ensure that the object was not
  // released before the call to PlasmaClient::Seal.
  return Release(object_id);
}

Status PlasmaClient::Delete(const ObjectID& object_id) {
  // TODO(rkn): In the future, we can use this method to give hints to the
  // eviction policy about when an object will no longer be needed.
  return Status::NotImplemented("PlasmaClient::Delete is not implemented.");
}

Status PlasmaClient::Evict(int64_t num_bytes, int64_t& num_bytes_evicted) {
  // Send a request to the store to evict objects.
  RETURN_NOT_OK(SendEvictRequest(store_conn_, num_bytes));
  // Wait for a response with the number of bytes actually evicted.
  std::vector<uint8_t> buffer;
  int64_t type;
  RETURN_NOT_OK(ReadMessage(store_conn_, &type, &buffer));
  return ReadEvictReply(buffer.data(), num_bytes_evicted);
}

Status PlasmaClient::Hash(const ObjectID& object_id, uint8_t* digest) {
  // Get the plasma object data. We pass in a timeout of 0 to indicate that
  // the operation should timeout immediately.
  ObjectBuffer object_buffer;
  RETURN_NOT_OK(Get(&object_id, 1, 0, &object_buffer));
  // If the object was not retrieved, return false.
  if (object_buffer.data_size == -1) {
    return Status::PlasmaObjectNonexistent("Object not found");
  }
  // Compute the hash.
  uint64_t hash = compute_object_hash(object_buffer);
  memcpy(digest, &hash, sizeof(hash));
  // Release the plasma object.
  return Release(object_id);
}

Status PlasmaClient::Subscribe(int* fd) {
  int sock[2];
  // Create a non-blocking socket pair. This will only be used to send
  // notifications from the Plasma store to the client.
  socketpair(AF_UNIX, SOCK_STREAM, 0, sock);
  // Make the socket non-blocking.
  int flags = fcntl(sock[1], F_GETFL, 0);
  ARROW_CHECK(fcntl(sock[1], F_SETFL, flags | O_NONBLOCK) == 0);
  // Tell the Plasma store about the subscription.
  RETURN_NOT_OK(SendSubscribeRequest(store_conn_));
  // Send the file descriptor that the Plasma store should use to push
  // notifications about sealed objects to this client.
  ARROW_CHECK(send_fd(store_conn_, sock[1]) >= 0);
  close(sock[1]);
  // Return the file descriptor that the client should use to read notifications
  // about sealed objects.
  *fd = sock[0];
  return Status::OK();
}

Status PlasmaClient::GetNotification(
    int fd, ObjectID* object_id, int64_t* data_size, int64_t* metadata_size) {
  uint8_t* notification = read_message_async(fd);
  if (notification == NULL) {
    return Status::IOError("Failed to read object notification from Plasma socket");
  }
  auto object_info = flatbuffers::GetRoot<ObjectInfo>(notification);
  ARROW_CHECK(object_info->object_id()->size() == sizeof(ObjectID));
  memcpy(object_id, object_info->object_id()->data(), sizeof(ObjectID));
  if (object_info->is_deletion()) {
    *data_size = -1;
    *metadata_size = -1;
  } else {
    *data_size = object_info->data_size();
    *metadata_size = object_info->metadata_size();
  }
  delete[] notification;
  return Status::OK();
}

Status PlasmaClient::Connect(const std::string& store_socket_name,
    const std::string& manager_socket_name, int release_delay) {
  store_conn_ = connect_ipc_sock_retry(store_socket_name, -1, -1);
  if (manager_socket_name != "") {
    manager_conn_ = connect_ipc_sock_retry(manager_socket_name, -1, -1);
  } else {
    manager_conn_ = -1;
  }
  config_.release_delay = release_delay;
  in_use_object_bytes_ = 0;
  // Send a ConnectRequest to the store to get its memory capacity.
  RETURN_NOT_OK(SendConnectRequest(store_conn_));
  std::vector<uint8_t> buffer;
  RETURN_NOT_OK(PlasmaReceive(store_conn_, MessageType_PlasmaConnectReply, &buffer));
  RETURN_NOT_OK(ReadConnectReply(buffer.data(), &store_capacity_));
  return Status::OK();
}

Status PlasmaClient::Disconnect() {
  // NOTE: We purposefully do not finish sending release calls for objects in
  // use, so that we don't duplicate PlasmaClient::Release calls (when handling
  // a SIGTERM, for example).

  // Close the connections to Plasma. The Plasma store will release the objects
  // that were in use by us when handling the SIGPIPE.
  close(store_conn_);
  store_conn_ = -1;
  if (manager_conn_ >= 0) {
    close(manager_conn_);
    manager_conn_ = -1;
  }
  return Status::OK();
}

#define h_addr h_addr_list[0]

Status PlasmaClient::Transfer(const char* address, int port, const ObjectID& object_id) {
  return SendDataRequest(manager_conn_, object_id, address, port);
}

Status PlasmaClient::Fetch(int num_object_ids, const ObjectID* object_ids) {
  ARROW_CHECK(manager_conn_ >= 0);
  return SendFetchRequest(manager_conn_, object_ids, num_object_ids);
}

int PlasmaClient::get_manager_fd() {
  return manager_conn_;
}

Status PlasmaClient::Info(const ObjectID& object_id, int* object_status) {
  ARROW_CHECK(manager_conn_ >= 0);

  RETURN_NOT_OK(SendStatusRequest(manager_conn_, &object_id, 1));
  std::vector<uint8_t> buffer;
  RETURN_NOT_OK(PlasmaReceive(manager_conn_, MessageType_PlasmaStatusReply, &buffer));
  ObjectID id;
  RETURN_NOT_OK(ReadStatusReply(buffer.data(), &id, object_status, 1));
  ARROW_CHECK(object_id == id);
  return Status::OK();
}

Status PlasmaClient::Wait(int64_t num_object_requests, ObjectRequest* object_requests,
    int num_ready_objects, int64_t timeout_ms, int* num_objects_ready) {
  ARROW_CHECK(manager_conn_ >= 0);
  ARROW_CHECK(num_object_requests > 0);
  ARROW_CHECK(num_ready_objects > 0);
  ARROW_CHECK(num_ready_objects <= num_object_requests);

  for (int i = 0; i < num_object_requests; ++i) {
    ARROW_CHECK(object_requests[i].type == PLASMA_QUERY_LOCAL ||
                object_requests[i].type == PLASMA_QUERY_ANYWHERE);
  }

  RETURN_NOT_OK(SendWaitRequest(manager_conn_, object_requests, num_object_requests,
      num_ready_objects, timeout_ms));
  std::vector<uint8_t> buffer;
  RETURN_NOT_OK(PlasmaReceive(manager_conn_, MessageType_PlasmaWaitReply, &buffer));
  RETURN_NOT_OK(ReadWaitReply(buffer.data(), object_requests, &num_ready_objects));

  *num_objects_ready = 0;
  for (int i = 0; i < num_object_requests; ++i) {
    int type = object_requests[i].type;
    int status = object_requests[i].status;
    switch (type) {
      case PLASMA_QUERY_LOCAL:
        if (status == ObjectStatus_Local) { *num_objects_ready += 1; }
        break;
      case PLASMA_QUERY_ANYWHERE:
        if (status == ObjectStatus_Local || status == ObjectStatus_Remote) {
          *num_objects_ready += 1;
        } else {
          ARROW_CHECK(status == ObjectStatus_Nonexistent);
        }
        break;
      default:
        ARROW_LOG(FATAL) << "This code should be unreachable.";
    }
  }
  return Status::OK();
}

} // namespace plasma
