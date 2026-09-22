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

#include <stdexcept>

namespace pcdio {

// All PcdIO exceptions derive from std::runtime_error, so every error the library throws
// (including the std::runtime_error it raises for file I/O failures) is catchable as
// std::runtime_error, while the specific type still lets callers distinguish categories.

/// The input does not conform to the PCD format specification (e.g. malformed header, invalid
/// field descriptor, inconsistent spec on save).
struct InvalidFormat : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Loading a file that uses a PCD feature outside the supported subset (e.g. an unknown DATA
/// encoding, or a VERSION other than 0.7).  `validate_spec` classifies the same properties of an
/// in-memory spec as InvalidFormat: such a spec is invalid rather than unsupported.
struct UnsupportedFeature : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// The data section is corrupt (e.g. truncated, failed decompression, unparsable values).
struct CorruptData : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

} // namespace pcdio
