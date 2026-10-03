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

# The test executable links Catch2::Catch2WithMain; a parent project that vendors only
# Catch2::Catch2 must not trip this guard.
if(TARGET Catch2::Catch2WithMain)
    return()
endif()

message(STATUS "Third-party (external): creating target 'Catch2::Catch2'")

option(CATCH_CONFIG_CPP17_STRING_VIEW "Enable support for std::string_view" ON)
option(CATCH_INSTALL_DOCS "Install documentation alongside library" OFF)
option(CATCH_INSTALL_EXTRAS "Install extras alongside library" OFF)

include(CPM)
CPMAddPackage(
    NAME catch2
    GITHUB_REPOSITORY catchorg/Catch2
    # Commit of tag v3.10.0.  Pin the immutable commit, not the re-pointable tag (same pattern
    # as sanitizer-cmake.cmake).
    GIT_TAG 25319fd3047c6bdcf3c0170e76fa526c77f99ca9
    SYSTEM On
)

target_compile_features(Catch2 PUBLIC cxx_std_17)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(Catch2 PUBLIC -Wno-nonnull)

    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
        # See https://github.com/catchorg/Catch2/issues/2654
        target_compile_options(Catch2 PUBLIC -Wno-parentheses)
    endif()
endif()

set_target_properties(Catch2 PROPERTIES FOLDER third_party)
set_target_properties(Catch2WithMain PROPERTIES FOLDER third_party)
include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
