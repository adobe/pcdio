/*
 * Copyright 2026 Adobe. All rights reserved.
 * This file is licensed to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
 * OF ANY KIND, either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#pragma once

#include <cstddef>

namespace pcdio::internal {

///
/// Compress a buffer using the LZF algorithm (liblzf-compatible stream format).
///
/// @param[in]  in_data   Input buffer.
/// @param[in]  in_len    Number of input bytes.
/// @param[out] out_data  Output buffer.
/// @param[in]  out_len   Capacity of the output buffer in bytes.
///
/// @return     Number of compressed bytes written, or 0 if the output did not fit (or the input
///             was empty).
///
size_t lzf_compress(const void* in_data, size_t in_len, void* out_data, size_t out_len);

///
/// Decompress an LZF-compressed buffer (liblzf-compatible stream format).
///
/// @param[in]  in_data   Compressed input buffer.
/// @param[in]  in_len    Number of compressed input bytes.
/// @param[out] out_data  Output buffer.
/// @param[in]  out_len   Capacity of the output buffer in bytes (expected decompressed size).
///
/// @return     Number of decompressed bytes written, or 0 on error (corrupt input or insufficient
///             output capacity).
///
size_t lzf_decompress(const void* in_data, size_t in_len, void* out_data, size_t out_len);

} // namespace pcdio::internal
