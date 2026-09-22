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

#include <pcdio/exception.h>
#include <pcdio/pcdio.h>

#include "lzf.h"
#include "pcd_utils.h"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace pcdio {

namespace {

// Format a floating-point value with full round-trip precision.  std::to_chars is
// locale-independent (the PCD format always uses '.' as the decimal separator, regardless of
// the process locale), and its shortest representation round-trips exactly.
template <typename ValueType>
std::string format_float(ValueType value)
{
    char buf[32]; // The shortest float/double form provably fits (<= 24 chars).
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    if (result.ec != std::errc()) {
        throw std::runtime_error("PCD float formatting failed.");
    }
    return std::string(buf, result.ptr);
}

// Format a single field element (at `p`) as an ASCII token.
std::string ascii_token(const uint8_t* p, char type, int size)
{
    std::string token;
    internal::dispatch_pcd_type(type, size, [&](auto zero) {
        using ValueType = decltype(zero);
        ValueType value;
        std::memcpy(&value, p, sizeof(ValueType));
        if constexpr (std::is_floating_point_v<ValueType>) {
            token = format_float(value);
        } else if constexpr (std::is_signed_v<ValueType>) {
            token = std::to_string(static_cast<long long>(value));
        } else {
            token = std::to_string(static_cast<unsigned long long>(value));
        }
    });
    return token;
}

} // namespace

void save_pcd(std::ostream& out, const PcdSpec& spec)
{
    validate_spec(spec);

    // Header.
    out << "# .PCD v0.7 - Point Cloud Data file format\n";
    out << "VERSION " << spec.version << "\n";
    out << "FIELDS";
    for (const auto& field : spec.fields) out << ' ' << field.name;
    out << "\nSIZE";
    for (const auto& field : spec.fields) out << ' ' << field.size;
    out << "\nTYPE";
    for (const auto& field : spec.fields) out << ' ' << field.type;
    out << "\nCOUNT";
    for (const auto& field : spec.fields) out << ' ' << field.count;
    out << "\nWIDTH " << spec.width << "\n";
    out << "HEIGHT " << spec.height << "\n";
    out << "VIEWPOINT";
    for (double v : spec.viewpoint) out << ' ' << format_float(v);
    out << "\nPOINTS " << spec.points << "\n";
    out << "DATA " << spec.data << "\n";

    const size_t points = spec.points;

    if (spec.data == "ascii") {
        for (size_t i = 0; i < points; ++i) {
            bool first = true;
            for (const auto& field : spec.fields) {
                const uint8_t* record = field.data.data() + i * field.stride();
                for (int c = 0; c < field.count; ++c) {
                    if (!first) out << ' ';
                    first = false;
                    out << ascii_token(
                        record + static_cast<size_t>(c) * static_cast<size_t>(field.size),
                        field.type,
                        field.size);
                }
            }
            out << '\n';
        }
    } else if (spec.data == "binary") {
        // Row-major (AoS): each point record is the concatenation of its field values.
        for (size_t i = 0; i < points; ++i) {
            for (const auto& field : spec.fields) {
                out.write(reinterpret_cast<const char*>(field.data.data() + i * field.stride()),
                    static_cast<std::streamsize>(field.stride()));
            }
        }
    } else {
        // binary_compressed: compress each field as an independent LZF stream and concatenate
        // the results.  A concatenation of self-contained LZF streams decodes as a single
        // stream producing the concatenation of the inputs (back-references never cross field
        // boundaries), which is exactly the column-major (SoA) payload layout the decoder
        // expects.  This avoids staging a second full copy of the cloud, keeping peak memory
        // near payload + compressed output instead of 3x the payload.
        size_t uncompressed_size = 0;
        for (const auto& field : spec.fields) {
            if (field.data.size() > std::numeric_limits<size_t>::max() - uncompressed_size) {
                throw InvalidFormat("PCD binary_compressed payload size overflows.");
            }
            uncompressed_size += field.data.size();
        }
        if (uncompressed_size > std::numeric_limits<uint32_t>::max()) {
            throw InvalidFormat("PCD binary_compressed payload exceeds the 4 GiB format limit.");
        }

        std::vector<uint8_t> compressed_buf;
        for (const auto& field : spec.fields) {
            const size_t field_bytes = field.data.size();
            if (field_bytes == 0) continue;
            const size_t capacity = field_bytes + field_bytes / 16 + 64;
            const size_t base = compressed_buf.size();
            compressed_buf.resize(base + capacity);
            const size_t n = internal::lzf_compress(
                field.data.data(), field_bytes, compressed_buf.data() + base, capacity);
            if (n == 0) throw CorruptData("PCD LZF compression failed.");
            compressed_buf.resize(base + n);
        }
        if (compressed_buf.size() > std::numeric_limits<uint32_t>::max()) {
            throw InvalidFormat(
                "PCD binary_compressed compressed payload exceeds the 4 GiB format limit.");
        }
        const uint32_t compressed_size = static_cast<uint32_t>(compressed_buf.size());
        const uint32_t usize = static_cast<uint32_t>(uncompressed_size);
        out.write(reinterpret_cast<const char*>(&compressed_size), sizeof(uint32_t));
        out.write(reinterpret_cast<const char*>(&usize), sizeof(uint32_t));
        out.write(reinterpret_cast<const char*>(compressed_buf.data()),
            static_cast<std::streamsize>(compressed_size));
    }

    // Streams report write failures through their state, not exceptions; surface them.
    out.flush();
    if (!out) {
        throw std::runtime_error("Failed to write PCD output stream.");
    }
}

void save_pcd(const std::string& filename, const PcdSpec& spec)
{
    // Validate before opening: ofstream truncates an existing destination immediately, and a
    // validation failure must not destroy the previous file.
    validate_spec(spec);
    std::ofstream fout(filename.c_str(), std::ios::binary);
    if (!fout.is_open()) {
        throw std::runtime_error("Unable to open output file: " + filename);
    }
    save_pcd(fout, spec);
    fout.close();
    if (!fout) {
        throw std::runtime_error("Unable to write output file: " + filename);
    }
}

} // namespace pcdio
