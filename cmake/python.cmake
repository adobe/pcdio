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
if (SKBUILD)
    message(STATUS "Use scikit-build python environment")
    message(STATUS "Python_VERSION ${PYTHON_VERSION_STRING}")
    message(STATUS "Python_EXECUTABLE ${PYTHON_EXECUTABLE}")
    message(STATUS "Python_INCLUDE_DIR ${PYTHON_INCLUDE_DIR}")
    message(STATUS "Python_LIBRARIES ${PYTHON_LIBRARY}")
    set(Python_VERSION "${PYTHON_VERSION_STRING}")
    set(Python_EXECUTABLE "${PYTHON_EXECUTABLE}")
    set(Python_INCLUDE_DIR "${PYTHON_INCLUDE_DIR}")
    set(Python_LIBRARIES "${PYTHON_LIBRARY}")
else()
    set(Python_FIND_VIRTUALENV FIRST)
endif()

# The FindPython components the binding needs postdate the project's 3.14 C++-only floor:
# Development.Module requires CMake >= 3.18, and Development.SABIModule (Windows split mode)
# requires >= 3.26.  Fail fast with a clear message instead of a cryptic "unknown component"
# error.  scikit-build-core wheel builds supply a modern FindPython, so this never bites there.
set(pcdio_python_components Interpreter Development.Module)
set(pcdio_python_min_cmake 3.18)
if (WIN32)
    list(APPEND pcdio_python_components Development.SABIModule)
    set(pcdio_python_min_cmake 3.26)
endif()
if (CMAKE_VERSION VERSION_LESS "${pcdio_python_min_cmake}")
    message(FATAL_ERROR
        "Building the pcdio Python module (PCDIO_PYTHON=ON) requires CMake >= "
        "${pcdio_python_min_cmake}; the C++ library alone needs only 3.14.")
endif()

find_package(Python COMPONENTS ${pcdio_python_components} REQUIRED)

