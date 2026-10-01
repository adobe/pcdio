#
# Copyright 2026 Adobe. All rights reserved.
# This file is licensed to you under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License. You may obtain a copy
# of the License at http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under
# the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
# OF ANY KIND, either express or implied. See the License for the specific language
# governing permissions and limitations under the License.
#
if(COMMAND add_sanitizers)
    return()
endif()

message(STATUS "Third-party (external): creating method 'add_sanitizers'")

include(CPM)
CPMAddPackage(
    NAME sanitizer
    GITHUB_REPOSITORY arsenm/sanitizers-cmake
    GIT_TAG 0573e2ea8651b9bb3083f193c41eb086497cc80a
    DOWNLOAD_ONLY ON
)

# Save/restore CMAKE_MODULE_PATH by hand: block()/endblock() would scope this for us but
# requires CMake 3.25, while this project supports 3.14.
set(sanitizer_module_path_backup "${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH "${sanitizer_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
find_package(Sanitizers REQUIRED)
set(CMAKE_MODULE_PATH "${sanitizer_module_path_backup}")
unset(sanitizer_module_path_backup)
