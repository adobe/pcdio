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

// The documented @throws contract of this header names the exception types; expose them so
// consumers can write catch clauses after including only this umbrella header.
#include <pcdio/PcdSpec.h>
#include <pcdio/exception.h>

#include <istream>
#include <ostream>
#include <string>

namespace pcdio {

///
/// Loads a point cloud in PCD (Point Cloud Data) format from an input stream.
///
/// All three PCD data encodings are supported: `ascii`, `binary` and `binary_compressed`
/// (LZF-compressed, as produced by PCL).  Every field is preserved verbatim in
/// `PcdSpec::fields`; no type conversion is applied.  Binary payloads (including the
/// `binary_compressed` size prefixes) use the host's native byte order — identical to PCL's
/// own reader/writer, which also performs no byte swapping; PCD files are little-endian in
/// practice on every platform PCL supports.
///
/// @param[in]  in  Input stream.
///
/// @return     The loaded point cloud.
///
/// @throws     pcdio::InvalidFormat, pcdio::UnsupportedFeature, pcdio::CorruptData
///
PcdSpec load_pcd(std::istream& in);

///
/// @overload
///
/// Loads a point cloud in PCD (Point Cloud Data) format from a file.
///
/// @param[in]  filename  Input filename.
///
/// @return     The loaded point cloud.
///
/// @throws     std::runtime_error if the file cannot be opened, in addition to the exceptions
///             documented for the stream overload.
///
PcdSpec load_pcd(const std::string& filename);

///
/// Saves a point cloud in PCD (Point Cloud Data) format to an output stream.
///
/// The data encoding is controlled by `spec.data`: "ascii", "binary" or "binary_compressed"
/// (LZF-compressed).  The stream must be opened in binary mode for the binary encodings.
/// `spec` is validated with @ref validate_spec before anything is written.
///
/// @param[in,out] out   Output stream.
/// @param[in]     spec  Point cloud to write.
///
/// @throws        pcdio::InvalidFormat (invalid spec), pcdio::CorruptData (compression
///                failure), and std::runtime_error if a write or flush fails on the stream.
///
void save_pcd(std::ostream& out, const PcdSpec& spec);

///
/// @overload
///
/// Saves a point cloud in PCD (Point Cloud Data) format to a file.
///
/// @param[in]  filename  Output filename.
/// @param[in]  spec      Point cloud to write.
///
void save_pcd(const std::string& filename, const PcdSpec& spec);

///
/// Checks that a PcdSpec is self-consistent and writable.
///
/// @param[in]  spec  Point cloud to validate.
///
/// @throws     pcdio::InvalidFormat if the spec is invalid.
///
void validate_spec(const PcdSpec& spec);

} // namespace pcdio
