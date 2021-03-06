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

#ifndef PLASMA_PLASMA_H
#define PLASMA_PLASMA_H

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // pid_t

#include <unordered_map>
#include <unordered_set>

#include "arrow/status.h"
#include "arrow/util/logging.h"
#include "plasma/common.h"
#include "plasma/common_generated.h"

namespace plasma {

#define HANDLE_SIGPIPE(s, fd_)                                              \
  do {                                                                      \
    Status _s = (s);                                                        \
    if (!_s.ok()) {                                                         \
      if (errno == EPIPE || errno == EBADF || errno == ECONNRESET) {        \
        ARROW_LOG(WARNING)                                                  \
            << "Received SIGPIPE, BAD FILE DESCRIPTOR, or ECONNRESET when " \
               "sending a message to client on fd "                         \
            << fd_ << ". "                                                  \
                      "The client on the other end may have hung up.";      \
      } else {                                                              \
        return _s;                                                          \
      }                                                                     \
    }                                                                       \
  } while (0);

/// Allocation granularity used in plasma for object allocation.
#define BLOCK_SIZE 64

struct Client;

/// Mapping from object IDs to type and status of the request.
typedef std::unordered_map<ObjectID, ObjectRequest, UniqueIDHasher> ObjectRequestMap;

/// Handle to access memory mapped file and map it into client address space.
struct object_handle {
  /// The file descriptor of the memory mapped file in the store. It is used as
  /// a unique identifier of the file in the client to look up the corresponding
  /// file descriptor on the client's side.
  int store_fd;
  /// The size in bytes of the memory mapped file.
  int64_t mmap_size;
};

// TODO(pcm): Replace this by the flatbuffers message PlasmaObjectSpec.
struct PlasmaObject {
  /// Handle for memory mapped file the object is stored in.
  object_handle handle;
  /// The offset in bytes in the memory mapped file of the data.
  ptrdiff_t data_offset;
  /// The offset in bytes in the memory mapped file of the metadata.
  ptrdiff_t metadata_offset;
  /// The size in bytes of the data.
  int64_t data_size;
  /// The size in bytes of the metadata.
  int64_t metadata_size;
};

enum object_state {
  /// Object was created but not sealed in the local Plasma Store.
  PLASMA_CREATED = 1,
  /// Object is sealed and stored in the local Plasma Store.
  PLASMA_SEALED
};

enum object_status {
  /// The object was not found.
  OBJECT_NOT_FOUND = 0,
  /// The object was found.
  OBJECT_FOUND = 1
};

/// This type is used by the Plasma store. It is here because it is exposed to
/// the eviction policy.
struct ObjectTableEntry {
  /// Object id of this object.
  ObjectID object_id;
  /// Object info like size, creation time and owner.
  ObjectInfoT info;
  /// Memory mapped file containing the object.
  int fd;
  /// Size of the underlying map.
  int64_t map_size;
  /// Offset from the base of the mmap.
  ptrdiff_t offset;
  /// Pointer to the object data. Needed to free the object.
  uint8_t* pointer;
  /// Set of clients currently using this object.
  std::unordered_set<Client*> clients;
  /// The state of the object, e.g., whether it is open or sealed.
  object_state state;
  /// The digest of the object. Used to see if two objects are the same.
  unsigned char digest[kDigestSize];
};

/// The plasma store information that is exposed to the eviction policy.
struct PlasmaStoreInfo {
  /// Objects that are in the Plasma store.
  std::unordered_map<ObjectID, std::unique_ptr<ObjectTableEntry>, UniqueIDHasher> objects;
  /// The amount of memory (in bytes) that we allow to be allocated in the
  /// store.
  int64_t memory_capacity;
};

/// Get an entry from the object table and return NULL if the object_id
/// is not present.
///
/// @param store_info The PlasmaStoreInfo that contains the object table.
/// @param object_id The object_id of the entry we are looking for.
/// @return The entry associated with the object_id or NULL if the object_id
///         is not present.
ObjectTableEntry* get_object_table_entry(
    PlasmaStoreInfo* store_info, const ObjectID& object_id);

/// Print a warning if the status is less than zero. This should be used to check
/// the success of messages sent to plasma clients. We print a warning instead of
/// failing because the plasma clients are allowed to die. This is used to handle
/// situations where the store writes to a client file descriptor, and the client
/// may already have disconnected. If we have processed the disconnection and
/// closed the file descriptor, we should get a BAD FILE DESCRIPTOR error. If we
/// have not, then we should get a SIGPIPE. If we write to a TCP socket that
/// isn't connected yet, then we should get an ECONNRESET.
///
/// @param status The status to check. If it is less less than zero, we will
///        print a warning.
/// @param client_sock The client socket. This is just used to print some extra
///        information.
/// @return The errno set.
int warn_if_sigpipe(int status, int client_sock);

uint8_t* create_object_info_buffer(ObjectInfoT* object_info);

} // namespace plasma

#endif  // PLASMA_PLASMA_H
