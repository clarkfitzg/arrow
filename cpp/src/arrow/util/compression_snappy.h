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

#ifndef ARROW_UTIL_COMPRESSION_SNAPPY_H
#define ARROW_UTIL_COMPRESSION_SNAPPY_H

#include <cstdint>
#include <memory>

#include "arrow/status.h"
#include "arrow/util/compression.h"

namespace arrow {

class ARROW_EXPORT SnappyCodec : public Codec {
 public:
  Status Decompress(int64_t input_len, const uint8_t* input, int64_t output_len,
      uint8_t* output_buffer) override;

  Status Compress(int64_t input_len, const uint8_t* input, int64_t output_buffer_len,
      uint8_t* output_buffer, int64_t* output_length) override;

  int64_t MaxCompressedLen(int64_t input_len, const uint8_t* input) override;

  const char* name() const override { return "snappy"; }
};

}  // namespace arrow

#endif  // ARROW_UTIL_COMPRESSION_SNAPPY_H
