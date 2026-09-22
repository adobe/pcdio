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

#include <pcdio/PcdSpec.h>
#include <pcdio/exception.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>

namespace pcdio::internal {

///
/// Dispatches a (type, size) pair from a PCD header to the matching C++ value type and invokes
/// `func` with a value-initialized instance of that type.
///
/// @param[in]  type  PCD type character ('I', 'U', or 'F').
/// @param[in]  size  Size in bytes of the field element (1, 2, 4, or 8).
/// @param[in]  func  Callable invoked as `func(ValueType{})`.
///
template <typename Func>
void dispatch_pcd_type(char type, int size, Func&& func)
{
    switch (type) {
    case 'F':
        if (size == 4) return void(func(float{}));
        if (size == 8) return void(func(double{}));
        break;
    case 'I':
        if (size == 1) return void(func(int8_t{}));
        if (size == 2) return void(func(int16_t{}));
        if (size == 4) return void(func(int32_t{}));
        if (size == 8) return void(func(int64_t{}));
        break;
    case 'U':
        if (size == 1) return void(func(uint8_t{}));
        if (size == 2) return void(func(uint16_t{}));
        if (size == 4) return void(func(uint32_t{}));
        if (size == 8) return void(func(uint64_t{}));
        break;
    default: break;
    }
    throw UnsupportedFeature("Unsupported PCD field type '" + std::string(1, type) +
                             "' with size " + std::to_string(size));
}

///
/// Formats a floating-point value with round-trip (max_digits10) precision using the classic "C"
/// locale, so the PCD '.' decimal separator is emitted regardless of the process locale.
///
/// This intentionally avoids std::to_chars(float/double): libc++ marks the floating-point
/// charconv overloads unavailable below very recent Apple/emscripten platforms, so they cannot be
/// used at Lagrange's deployment target. A classic-locale std::ostringstream is portable across
/// every supported compiler and, at max_digits10 precision, round-trips the value exactly.
///
template <typename ValueType>
std::string format_float(ValueType value)
{
    static_assert(std::is_floating_point_v<ValueType>, "format_float requires a floating type.");
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(std::numeric_limits<ValueType>::max_digits10) << value;
    return oss.str();
}

///
/// Parses a floating-point token in the classic "C" locale (locale-independent '.' decimal),
/// writing the result to `out`. The whole [begin, end) range must be consumed. "nan"/"inf"/"-inf"
/// are accepted (PCL emits them for invalid measurements).
///
/// Portable replacement for std::from_chars(double), which libc++ marks unavailable below very
/// recent Apple/emscripten platforms.
///
/// @param[in]   begin         First character of the token.
/// @param[in]   end           One past the last character of the token.
/// @param[out]  out           Parsed value on success; unspecified on failure.
/// @param[out]  out_of_range  Set to true when the token overflowed the double range.
///
/// @return      true on success (fully consumed, well-formed); false otherwise.
///
inline bool parse_double(const char* begin, const char* end, double& out, bool& out_of_range)
{
    out_of_range = false;
    out = 0.0;

    // Handle non-finite tokens explicitly: num_get support for "inf"/"nan" is inconsistent across
    // standard libraries, whereas PCL routinely emits them for invalid measurements.
    {
        const char* p = begin;
        bool negative = false;
        if (p != end && (*p == '+' || *p == '-')) {
            negative = (*p == '-');
            ++p;
        }
        auto ci_equals = [](const char* a, const char* a_end, const char* lit) {
            for (; a != a_end && *lit != '\0'; ++a, ++lit) {
                const unsigned char c = static_cast<unsigned char>(*a);
                const unsigned char folded =
                    (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
                if (folded != static_cast<unsigned char>(*lit)) {
                    return false;
                }
            }
            return a == a_end && *lit == '\0';
        };
        if (ci_equals(p, end, "inf") || ci_equals(p, end, "infinity")) {
            out = negative ? -std::numeric_limits<double>::infinity()
                           : std::numeric_limits<double>::infinity();
            return true;
        }
        if (ci_equals(p, end, "nan")) {
            out = std::numeric_limits<double>::quiet_NaN();
            return true;
        }
    }

    std::istringstream iss(std::string(begin, end));
    iss.imbue(std::locale::classic());
    iss >> out;
    if (iss.fail()) {
        // Since C++11, operator>> sets failbit and stores a saturated magnitude on overflow;
        // distinguish that from malformed input so callers can report it precisely.
        if (!iss.bad() &&
            (std::isinf(out) || std::abs(out) == std::numeric_limits<double>::max())) {
            out_of_range = true;
        }
        return false;
    }
    // Require the entire token to be consumed (matches std::from_chars, which reports a trailing
    // remainder); tokens are already whitespace-delimited, so anything left is junk.
    if (iss.peek() != std::char_traits<char>::eof()) return false;
    return true;
}

} // namespace pcdio::internal
