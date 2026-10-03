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

# ============================================================================
# This file will generate the cmake target `pcdio::pcdio`.
#
# To add PcdIO as a dependency:
#
#   add_subdirectory(pcdio)
#   target_link_libraries(your_target pcdio::pcdio)
#
# ============================================================================
message(STATUS "CMake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 3.14)

# Version tracks python/pcdio/__init__.py.
project(PcdIO VERSION 0.1.0)

option(PCDIO_BUILD_TESTS "Build unit tests" OFF)
option(PCDIO_BUILD_EXAMPLES "Build examples" OFF)
option(PCDIO_PYTHON "Build python binding" OFF)

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

# Included unconditionally so CMAKE_INSTALL_INCLUDEDIR agrees between the target's
# INSTALL_INTERFACE and the install rules below.
include(GNUInstallDirs)

# CONFIGURE_DEPENDS re-runs the glob at build time so newly added sources/headers trigger a
# reconfigure instead of being silently omitted from incremental builds.
file(GLOB INC_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/pcdio/*.h")
file(GLOB SRC_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")

add_library(pcdio STATIC ${SRC_FILES} ${INC_FILES})
target_include_directories(pcdio PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
set_property(TARGET pcdio PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(pcdio PUBLIC cxx_std_17)

add_library(pcdio::pcdio ALIAS pcdio)


if (PCDIO_PYTHON)
    include(nanobind)
    set(PY_SRC_FILE "${PROJECT_SOURCE_DIR}/python/pypcdio.cpp")
    nanobind_add_module(pypcdio ${PY_SRC_FILE} BACKEND_MODULE nanobind_backend)
    target_link_libraries(pypcdio PUBLIC pcdio::pcdio)
    if (DEFINED SKBUILD)
        # Windows .pyd modules are RUNTIME artifacts; declare both destinations so the wheel
        # contains the extension on every platform.
        install(TARGETS pypcdio
                LIBRARY DESTINATION ${SKBUILD_PROJECT_NAME}
                RUNTIME DESTINATION ${SKBUILD_PROJECT_NAME})
    else()
        install(TARGETS pypcdio
                LIBRARY DESTINATION .
                RUNTIME DESTINATION .)
    endif()
    add_library(pcdio::pypcdio ALIAS pypcdio)
    set_property(TARGET pypcdio PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()


if (PCDIO_BUILD_EXAMPLES)
    add_executable(pcd_inspect ${PROJECT_SOURCE_DIR}/examples/pcd_inspect.cpp)
    target_link_libraries(pcd_inspect PRIVATE pcdio::pcdio)
endif()


if (PCDIO_BUILD_TESTS)
    include(CTest)
    enable_testing()
    include(Catch2)
    include(sanitizer-cmake)

    file(GLOB TEST_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/*.cpp")
    add_executable(test_pcdio ${TEST_FILES})
    target_link_libraries(test_pcdio pcdio::pcdio Catch2::Catch2WithMain)
    set_property(TARGET test_pcdio PROPERTY POSITION_INDEPENDENT_CODE ON)
    catch_discover_tests(test_pcdio)

    if(NOT MSVC)
        target_compile_options(test_pcdio PRIVATE -Wconversion -Wall -Werror)
    else()
        target_compile_options(test_pcdio PRIVATE "/MP")
    endif()

    if (SANITIZE_ADDRESS OR
            SANITIZE_LINK_STATIC OR
            SANITIZE_MEMORY OR
            SANITIZE_THREAD OR
            SANITIZE_UNDEFINED)
        add_sanitizers(test_pcdio)
    endif()
endif()


# Wheels omit C++ development artifacts; ordinary builds can install both APIs.
if (NOT SKBUILD)
    include(CMakePackageConfigHelpers)
    install(TARGETS pcdio
            EXPORT pcdio_target
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h")
    # Generate and install a package config file so installed PcdIO is discoverable via
    # find_package(pcdio CONFIG).  The version tracks python/pcdio/__init__.py.
    configure_package_config_file(
            "${PROJECT_SOURCE_DIR}/cmake/pcdioConfig.cmake.in"
            "${PROJECT_BINARY_DIR}/pcdioConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pcdio)
    write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/pcdioConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)
    install(FILES
            "${PROJECT_BINARY_DIR}/pcdioConfig.cmake"
            "${PROJECT_BINARY_DIR}/pcdioConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pcdio)
    install(EXPORT pcdio_target
            NAMESPACE pcdio::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pcdio)
endif()
