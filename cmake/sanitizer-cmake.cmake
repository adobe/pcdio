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
