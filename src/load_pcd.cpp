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

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace pcdio {

namespace {

struct PcdHeader
{
    std::vector<PcdField> fields; // field metadata; data buffers are filled during load
    std::string version;
    size_t width = 0;
    size_t height = 1;
    size_t points = 0;
    // Presence tracking: VERSION, WIDTH and HEIGHT are required PCD 0.7 entries; a missing one
    // must not be indistinguishable from a parsed default.  COUNT, POINTS and VIEWPOINT remain
    // optional (COUNT defaults to 1, POINTS to WIDTH * HEIGHT).
    bool has_version = false;
    bool has_width = false;
    bool has_height = false;
    bool has_points = false;
    std::array<double, 7> viewpoint = {{0, 0, 0, 1, 0, 0, 0}};
    std::string data;       // "ascii", "binary", or "binary_compressed"
    size_t record_size = 0; // tightly packed size of a single point record
};

[[noreturn]] void invalid(const std::string& message)
{
    throw InvalidFormat(message);
}

[[noreturn]] void corrupt(const std::string& message)
{
    throw CorruptData(message);
}

size_t checked_mul(size_t a, size_t b, const char* message)
{
    if (b != 0 && a > std::numeric_limits<size_t>::max() / b) invalid(message);
    return a * b;
}

size_t checked_add(size_t a, size_t b, const char* message)
{
    if (a > std::numeric_limits<size_t>::max() - b) invalid(message);
    return a + b;
}

// Parse a single non-negative integer scalar header entry (WIDTH/HEIGHT/POINTS).  The token
// must be present and fully consumed; a leading minus sign and overflow are rejected by
// from_chars.
size_t parse_header_scalar(const std::string& key, std::istringstream& iss)
{
    std::string token;
    if (!(iss >> token)) {
        invalid("PCD header " + key + " entry is missing a value.");
    }
    size_t value = 0;
    const char* begin = token.data();
    const auto result = std::from_chars(begin, begin + token.size(), value);
    if (result.ec != std::errc() || result.ptr != begin + token.size()) {
        invalid("PCD header " + key + " entry '" + token + "' must be a non-negative integer.");
    }
    return value;
}

// Fixed-arity header entries (VERSION/WIDTH/HEIGHT/POINTS/VIEWPOINT/DATA) must not carry
// trailing values; reject them rather than silently ignoring content.
void reject_extra_tokens(const std::string& key, std::istringstream& iss)
{
    std::string extra;
    if (iss >> extra) {
        invalid("PCD header " + key + " entry has unexpected trailing value '" + extra + "'.");
    }
}

// Parse all remaining tokens of a header line (SIZE/COUNT) as integers, requiring full token
// consumption so trailing junk cannot be silently dropped.
std::vector<int> parse_int_list(const std::string& key, std::istringstream& iss)
{
    std::vector<int> values;
    std::string token;
    while (iss >> token) {
        int value = 0;
        const char* begin = token.data();
        const auto result = std::from_chars(begin, begin + token.size(), value);
        if (result.ec != std::errc() || result.ptr != begin + token.size()) {
            invalid("PCD header " + key + " entry '" + token + "' must be an integer.");
        }
        values.push_back(value);
    }
    return values;
}

PcdHeader parse_header(std::istream& in)
{
    PcdHeader header;
    std::vector<std::string> names;
    std::vector<std::string> types;
    std::vector<int> sizes;
    std::vector<int> counts;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if (key.empty() || key[0] == '#') continue;

        if (key == "VERSION") {
            iss >> header.version;
            header.has_version = true;
            reject_extra_tokens(key, iss);
        } else if (key == "FIELDS" || key == "COLUMNS") {
            names.clear();
            std::string token;
            while (iss >> token) names.push_back(token);
        } else if (key == "SIZE") {
            sizes = parse_int_list(key, iss);
        } else if (key == "TYPE") {
            types.clear();
            std::string token;
            while (iss >> token) types.push_back(token);
        } else if (key == "COUNT") {
            counts = parse_int_list(key, iss);
        } else if (key == "WIDTH") {
            header.width = parse_header_scalar(key, iss);
            header.has_width = true;
            reject_extra_tokens(key, iss);
        } else if (key == "HEIGHT") {
            header.height = parse_header_scalar(key, iss);
            header.has_height = true;
            reject_extra_tokens(key, iss);
        } else if (key == "VIEWPOINT") {
            for (auto& v : header.viewpoint) {
                std::string token;
                if (!(iss >> token)) {
                    invalid("PCD header VIEWPOINT entry must have 7 values.");
                }
                // Locale-independent like the payload parser: stream extraction would consult
                // the global C++ locale and misparse '.' decimals under e.g. de_DE.
                const char* begin = token.data();
                const auto result = std::from_chars(begin, begin + token.size(), v);
                if (result.ec != std::errc() || result.ptr != begin + token.size()) {
                    invalid("PCD header VIEWPOINT entry '" + token + "' could not be parsed.");
                }
            }
            reject_extra_tokens(key, iss);
        } else if (key == "POINTS") {
            header.points = parse_header_scalar(key, iss);
            header.has_points = true;
            reject_extra_tokens(key, iss);
        } else if (key == "DATA") {
            iss >> header.data;
            reject_extra_tokens(key, iss);
            break; // Data section starts on the next byte/line.
        }
        // Unknown keys are ignored.
    }

    if (!header.has_version) invalid("PCD file has no VERSION entry.");
    if (header.version.empty()) invalid("PCD header VERSION entry is missing a value.");
    if (header.version != "0.7") {
        throw UnsupportedFeature(
            "Unsupported PCD version '" + header.version + "'; only 0.7 is supported.");
    }
    if (!header.has_width) invalid("PCD file has no WIDTH entry.");
    if (!header.has_height) invalid("PCD file has no HEIGHT entry.");
    if (names.empty()) invalid("PCD file has no FIELDS entry.");
    if (sizes.size() != names.size() || types.size() != names.size()) {
        invalid("PCD header FIELDS/SIZE/TYPE entries have mismatched lengths.");
    }
    if (!counts.empty() && counts.size() != names.size()) {
        invalid("PCD header COUNT entry has " + std::to_string(counts.size()) + " values for " +
                std::to_string(names.size()) + " fields.");
    }
    if (header.data.empty()) invalid("PCD file has no DATA entry.");

    // Validate and lay out fields with overflow-checked arithmetic.
    std::set<std::string> seen_names;
    size_t offset = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        PcdField field;
        field.name = names[i];
        field.type = types[i].size() == 1 ? types[i][0] : '\0';
        field.size = sizes[i];
        field.count = counts.empty() ? 1 : counts[i];

        if (field.name.empty() || field.name.find_first_of(" \t\r\n") != std::string::npos) {
            invalid("PCD field name is empty or contains whitespace.");
        }
        if (!seen_names.insert(field.name).second) {
            invalid("PCD field name '" + field.name + "' is duplicated.");
        }
        if (field.type != 'I' && field.type != 'U' && field.type != 'F') {
            invalid("PCD field '" + field.name + "' has invalid TYPE.");
        }
        if (field.size != 1 && field.size != 2 && field.size != 4 && field.size != 8) {
            invalid("PCD field '" + field.name + "' has invalid SIZE " +
                    std::to_string(field.size) + ".");
        }
        if (field.type == 'F' && field.size != 4 && field.size != 8) {
            invalid("PCD field '" + field.name + "' has invalid float SIZE " +
                    std::to_string(field.size) + ".");
        }
        if (field.count < 1) {
            invalid("PCD field '" + field.name + "' has invalid COUNT " +
                    std::to_string(field.count) + ".");
        }

        offset = checked_add(offset,
            checked_mul(static_cast<size_t>(field.size),
                static_cast<size_t>(field.count),
                "PCD header size overflow."),
            "PCD header size overflow.");
        header.fields.push_back(std::move(field));
    }
    header.record_size = offset;

    const size_t expected_points =
        checked_mul(header.width, header.height, "PCD header size overflow.");
    if (header.has_points) {
        if (header.points != expected_points) {
            invalid("PCD header POINTS (" + std::to_string(header.points) +
                    ") does not equal WIDTH * HEIGHT (" + std::to_string(header.width) + " * " +
                    std::to_string(header.height) + ").");
        }
    } else {
        header.points = expected_points;
    }
    return header;
}

// Parse a single ASCII token into a field's native binary representation, written at `dst`.
// Parsing uses std::from_chars, which is locale-independent (the PCD format always uses '.'
// as the decimal separator, regardless of the process locale).  The token must be consumed in
// full; integers must fit the field's native type (no silent narrowing); and finite
// floating-point tokens must fit the destination type — narrowing a finite value to infinity
// is undefined behavior, while explicit "nan"/"inf" tokens remain accepted (PCL emits them
// for invalid measurements).
void write_ascii_value(char* dst, char type, int size, const std::string& token)
{
    internal::dispatch_pcd_type(type, size, [&](auto zero) {
        using ValueType = decltype(zero);
        const char* begin = token.data();
        const char* end = begin + token.size();
        ValueType value{};
        if constexpr (std::is_floating_point_v<ValueType>) {
            double parsed = 0;
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec == std::errc::result_out_of_range) {
                corrupt("PCD ascii value '" + token + "' is out of range for its field type.");
            }
            if (result.ec == std::errc::invalid_argument || result.ptr != end) {
                corrupt("PCD ascii value '" + token + "' could not be parsed.");
            }
            if (std::isfinite(parsed) &&
                (parsed < -static_cast<double>(std::numeric_limits<ValueType>::max()) ||
                    parsed > static_cast<double>(std::numeric_limits<ValueType>::max()))) {
                corrupt("PCD ascii value '" + token + "' is out of range for its field type.");
            }
            value = static_cast<ValueType>(parsed);
        } else {
            // from_chars rejects a leading minus sign for unsigned types and reports overflow
            // against the field's native type directly.
            const auto result = std::from_chars(begin, end, value);
            if (result.ec == std::errc::result_out_of_range) {
                corrupt("PCD ascii value '" + token + "' is out of range for its field type.");
            }
            if (result.ec == std::errc::invalid_argument || result.ptr != end) {
                corrupt("PCD ascii value '" + token + "' could not be parsed.");
            }
        }
        std::memcpy(dst, &value, sizeof(ValueType));
    });
}

// Read exactly `num_bytes` from the stream.  The buffer grows only as content actually
// arrives, so a truncated (or tiny) file cannot force the full declared size to be allocated.
std::string read_payload(std::istream& in, size_t num_bytes, const char* truncated_message)
{
    std::string content;
    char chunk[64 * 1024];
    while (content.size() < num_bytes && in) {
        const size_t remaining = num_bytes - content.size();
        in.read(chunk, static_cast<std::streamsize>(std::min(remaining, sizeof(chunk))));
        const std::streamsize got = in.gcount();
        if (got > 0) content.append(chunk, static_cast<size_t>(got));
    }
    if (content.size() != num_bytes) corrupt(truncated_message);
    return content;
}

} // namespace

PcdSpec load_pcd(std::istream& in)
{
    PcdHeader header = parse_header(in);
    const size_t points = header.points;

    PcdSpec spec;
    spec.version = header.version;
    spec.width = header.width;
    spec.height = header.height;
    spec.points = points;
    spec.viewpoint = header.viewpoint;
    spec.data = header.data;

    // Field metadata comes straight from the parsed header; data buffers are filled per
    // encoding below.  Allocation is tied to the amount of payload actually read so that a
    // crafted header alone cannot trigger a huge allocation.
    spec.fields = std::move(header.fields);

    if (header.data == "ascii") {
        std::string line;
        size_t point = 0;
        while (point < points && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find_first_not_of(" \t") == std::string::npos) continue; // Skip blank lines.
            std::istringstream iss(line);
            for (auto& field : spec.fields) {
                for (int c = 0; c < field.count; ++c) {
                    std::string token;
                    if (!(iss >> token)) {
                        corrupt("PCD ascii row " + std::to_string(point) + " has too few values.");
                    }
                    char value[8];
                    write_ascii_value(value, field.type, field.size, token);
                    field.data.insert(
                        field.data.end(), value, value + static_cast<size_t>(field.size));
                }
            }
            std::string extra;
            if (iss >> extra) {
                corrupt(
                    "PCD ascii row " + std::to_string(point) + " has more values than declared.");
            }
            ++point;
        }
        if (point != points) {
            corrupt("PCD ascii data has " + std::to_string(point) + " points, expected " +
                    std::to_string(points) + ".");
        }
    } else if (header.data == "binary") {
        // Row-major (AoS) payload of exactly record_size * points bytes.  The PCD format packs
        // records tightly, so no per-point stride is inferred from the stream length (trailing
        // bytes must not shift records).
        const size_t expected =
            checked_mul(header.record_size, points, "PCD record buffer size overflow.");
        const std::string rest =
            read_payload(in, expected, "PCD binary data section is truncated.");

        // Scatter each field's column out of the AoS records.  `offset` is the field's byte
        // position within a record, accumulated in field order.
        size_t offset = 0;
        for (auto& field : spec.fields) {
            const size_t stride = field.stride();
            field.data.resize(checked_mul(points, stride, "PCD field buffer size overflow."));
            for (size_t i = 0; i < points; ++i) {
                std::memcpy(field.data.data() + i * stride,
                    rest.data() + i * header.record_size + offset,
                    stride);
            }
            offset += stride;
        }
    } else if (header.data == "binary_compressed") {
        // Layout: uint32 compressed_size, uint32 uncompressed_size, then the LZF-compressed
        // blob.  The decompressed data is stored column-major (all values of field 0, then
        // field 1, ...), which matches the point-major per-field layout of PcdField exactly.
        uint32_t compressed_size = 0;
        uint32_t uncompressed_size = 0;
        in.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
        in.read(reinterpret_cast<char*>(&uncompressed_size), sizeof(uncompressed_size));
        if (!in.good()) corrupt("PCD binary_compressed header is truncated.");

        const size_t expected =
            checked_mul(header.record_size, points, "PCD record buffer size overflow.");
        if (uncompressed_size != expected) {
            corrupt("PCD binary_compressed uncompressed size " + std::to_string(uncompressed_size) +
                    " does not match expected " + std::to_string(expected) + ".");
        }
        // The writer compresses each field as an independent LZF stream, each with up to
        // (field_bytes / 16 + 64) bytes of overhead, so the plausibility bound scales with the
        // field count instead of assuming a single stream.
        const size_t compressed_bound =
            checked_add(static_cast<size_t>(uncompressed_size) + uncompressed_size / 16,
                checked_mul(size_t{64}, spec.fields.size(), "PCD header size overflow."),
                "PCD header size overflow.");
        if (compressed_size > compressed_bound) {
            corrupt("PCD binary_compressed compressed size is implausibly large.");
        }
        // LZF expands by at most 88x (264 output bytes per 3-byte token), so a larger claimed
        // ratio cannot be backed by the actual blob; reject it before allocating.
        if (static_cast<uint64_t>(uncompressed_size) >
            static_cast<uint64_t>(compressed_size) * 88) {
            corrupt("PCD binary_compressed size fields imply an impossible LZF expansion ratio.");
        }

        std::string blob =
            read_payload(in, compressed_size, "PCD binary_compressed data section is truncated.");

        std::vector<char> soa(uncompressed_size);
        if (uncompressed_size > 0) {
            size_t n = internal::lzf_decompress(blob.data(), blob.size(), soa.data(), soa.size());
            if (n != uncompressed_size) corrupt("PCD LZF decompression failed.");
        }
        // Release the compressed blob before allocating the destination fields to keep peak
        // memory at (uncompressed payload + destination fields).
        std::string().swap(blob);

        // Slice the column-major (SoA) payload into per-field buffers.
        size_t col_base = 0;
        for (auto& field : spec.fields) {
            const size_t field_bytes =
                checked_mul(points, field.stride(), "PCD field buffer size overflow.");
            if (field_bytes == 0) continue;
            field.data.resize(field_bytes);
            std::memcpy(field.data.data(), soa.data() + col_base, field_bytes);
            col_base += field_bytes;
        }
    } else {
        throw UnsupportedFeature("Unknown PCD DATA format: '" + header.data + "'.");
    }

    // The header checks above mirror the spec contract; run the full validator so no malformed
    // file can escape the loader.
    validate_spec(spec);
    return spec;
}

PcdSpec load_pcd(const std::string& filename)
{
    std::ifstream fin(filename.c_str(), std::ios::binary);
    if (!fin.is_open()) {
        throw std::runtime_error("Unable to open input file: " + filename);
    }
    return load_pcd(fin);
}

} // namespace pcdio
