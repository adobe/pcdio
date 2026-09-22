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

#include <cstdint>
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

} // namespace pcdio::internal
