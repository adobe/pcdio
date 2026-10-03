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

#include <limits>
#include <set>
#include <string>

namespace pcdio {

namespace {

[[noreturn]] void invalid(const std::string& message)
{
    throw InvalidFormat(message);
}

} // namespace

void validate_spec(const PcdSpec& spec)
{
    if (spec.version != "0.7") {
        invalid("Unsupported PCD version '" + spec.version + "'; only 0.7 is supported.");
    }
    if (spec.fields.empty()) invalid("PCD spec has no fields.");
    if (spec.data != "ascii" && spec.data != "binary" && spec.data != "binary_compressed") {
        invalid("Unknown PCD DATA format: '" + spec.data + "'.");
    }

    // HEIGHT 0 is rejected rather than normalized to 1: a saved `HEIGHT 0` would load back as
    // `HEIGHT 1`, breaking the round-trip.
    if (spec.height == 0) {
        invalid("PCD HEIGHT must be at least 1.");
    }
    if (spec.width > std::numeric_limits<size_t>::max() / spec.height) {
        invalid("PCD width * height overflows.");
    }
    if (spec.width * spec.height != spec.points) {
        invalid("PCD POINTS (" + std::to_string(spec.points) + ") does not equal WIDTH * HEIGHT (" +
                std::to_string(spec.width) + " * " + std::to_string(spec.height) + ").");
    }

    std::set<std::string> names;
    for (const auto& field : spec.fields) {
        if (field.name.empty() || field.name.find_first_of(" \t\r\n") != std::string::npos) {
            invalid("PCD field name is empty or contains whitespace.");
        }
        if (!names.insert(field.name).second) {
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
        // The stride product itself can overflow size_t on 32-bit targets (e.g. size 8 with
        // count 2^29 wraps to 0); check it before using stride() anywhere below.  On 64-bit
        // this can never fire (count <= 2^31 - 1, size <= 8).
        if (static_cast<size_t>(field.count) >
            std::numeric_limits<size_t>::max() / static_cast<size_t>(field.size)) {
            invalid("PCD field '" + field.name + "' data size overflows.");
        }
        if (spec.points != 0 && field.stride() > std::numeric_limits<size_t>::max() / spec.points) {
            invalid("PCD field '" + field.name + "' data size overflows.");
        }
        if (field.data.size() != spec.points * field.stride()) {
            invalid("PCD field '" + field.name + "' has " + std::to_string(field.data.size()) +
                    " bytes of data, expected " + std::to_string(spec.points * field.stride()) +
                    ".");
        }
    }
}

} // namespace pcdio
