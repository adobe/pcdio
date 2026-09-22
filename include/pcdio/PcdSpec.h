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

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace pcdio {

///
/// Returns the PCD `TYPE` character corresponding to a C++ value type: signed integer ('I'),
/// unsigned integer ('U'), or floating point ('F').
///
template <typename ValueType>
constexpr char pcd_type_char()
{
    if constexpr (std::is_floating_point_v<ValueType>) {
        return 'F';
    } else if constexpr (std::is_signed_v<ValueType>) {
        return 'I';
    } else {
        return 'U';
    }
}

///
/// A single named field of a PCD point cloud (one entry of the `FIELDS` header line).
///
/// The field values are stored in `data` as tightly packed raw bytes in point-major order:
/// point `i`, channel `c` occupies the byte range
/// `[(i * count + c) * size, (i * count + c + 1) * size)`.  The C++ value type of a field is
/// implied by `type` and `size`:
///
/// | type | size | C++ type   |
/// | :--: | :--: | :--------: |
/// | 'F'  | 4    | `float`    |
/// | 'F'  | 8    | `double`   |
/// | 'I'  | 1    | `int8_t`   |
/// | 'I'  | 2    | `int16_t`  |
/// | 'I'  | 4    | `int32_t`  |
/// | 'I'  | 8    | `int64_t`  |
/// | 'U'  | 1    | `uint8_t`  |
/// | 'U'  | 2    | `uint16_t` |
/// | 'U'  | 4    | `uint32_t` |
/// | 'U'  | 8    | `uint64_t` |
///
struct PcdField
{
    std::string name; ///< Field name (e.g. "x", "intensity"). Must not contain whitespace.
    char type = 'F';  ///< 'I' (signed integer), 'U' (unsigned integer), or 'F' (floating point).
    int size = 4;     ///< Size in bytes of each value: 1, 2, 4, or 8 (only 4 or 8 for type 'F').
    int count = 1;    ///< Number of values per point.
    std::vector<uint8_t> data; ///< Raw point-major field data (num_points * count * size bytes).

    ///
    /// Number of bytes one point contributes to this field (`count * size`).
    ///
    std::size_t stride() const
    {
        return static_cast<std::size_t>(size) * static_cast<std::size_t>(count);
    }

    ///
    /// Typed view of the raw field data.  `T` must match the C++ type implied by `type`/`size`;
    /// a mismatch throws `std::invalid_argument` instead of silently misinterpreting the byte
    /// buffer.  The vector storage is obtained from `operator new`, which is sufficiently
    /// aligned for any scalar `T` since C++17.  Reading `T` objects out of byte storage relies
    /// on the type-punning behavior every mainstream compiler provides in C++17 (the standard
    /// only blesses it for implicit-lifetime types in C++20); this is the same approach PCL and
    /// other PCD readers use, and it is covered by the UBSan test runs.
    ///
    template <typename T>
    const T* get_data() const
    {
        check_type<T>();
        return reinterpret_cast<const T*>(data.data());
    }

    /// @overload
    template <typename T>
    T* get_data()
    {
        check_type<T>();
        return reinterpret_cast<T*>(data.data());
    }

private:
    template <typename T>
    void check_type() const
    {
        if (type != pcd_type_char<T>() || size != static_cast<int>(sizeof(T))) {
            throw std::invalid_argument(
                "get_data<T>: T does not match the field's declared type/size");
        }
    }
};

///
/// In-memory representation of a PCD (Point Cloud Data) file, version 0.7.
///
/// The structure maps almost verbatim to the entries of a PCD header:
///
/// | PcdSpec member | PCD header entry        |
/// | :------------- | :---------------------- |
/// | version        | VERSION                 |
/// | fields         | FIELDS, SIZE, TYPE, COUNT |
/// | width          | WIDTH                   |
/// | height         | HEIGHT                  |
/// | points         | POINTS                  |
/// | viewpoint      | VIEWPOINT               |
/// | data           | DATA                    |
///
struct PcdSpec
{
    std::string version = "0.7";  ///< PCD format version.  Only "0.7" is supported.
    std::vector<PcdField> fields; ///< Point fields.  Field names are arbitrary (e.g. "x",
                                  ///< "intensity"); no mesh semantics are imposed.
    std::size_t width = 0;        ///< Width of the cloud (number of points for unorganized clouds).
    std::size_t height = 1;       ///< Height of the cloud (1 for unorganized clouds).
    std::size_t points = 0;       ///< Number of points.  Must equal `width * height`.
    std::array<double, 7> viewpoint = {
        {0, 0, 0, 1, 0, 0, 0}};             ///< Sensor acquisition pose (tx ty tz qw qx qy qz).
    std::string data = "binary_compressed"; ///< Data encoding: "ascii", "binary" or
                                            ///< "binary_compressed" (LZF-compressed).

    ///
    /// Returns the field with the given name, or `nullptr` if no such field exists.
    ///
    const PcdField* find_field(const std::string& name) const
    {
        for (const auto& field : fields) {
            if (field.name == name) return &field;
        }
        return nullptr;
    }

    /// @overload
    PcdField* find_field(const std::string& name)
    {
        for (auto& field : fields) {
            if (field.name == name) return &field;
        }
        return nullptr;
    }
};

} // namespace pcdio
