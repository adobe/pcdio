include_guard(GLOBAL) # Sibling helper modules are all idempotent; match them.

set(CPM_DOWNLOAD_VERSION 0.42.0)

if(CPM_SOURCE_CACHE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

# Expand relative path. This is important if the provided path contains a tilde (~)
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

# Published SHA-256 of the CPM 0.42.0 release asset; the downloaded script is executed via
# include(), so its integrity MUST be verified before use.
set(CPM_DOWNLOAD_HASH
    "SHA256=2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a")

function(download_cpm)
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
       https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
       ${CPM_DOWNLOAD_LOCATION}
       TLS_VERIFY ON
       EXPECTED_HASH ${CPM_DOWNLOAD_HASH}
       STATUS download_status
  )
  list(GET download_status 0 download_status_code)
  list(GET download_status 1 download_status_message)
  if(NOT download_status_code EQUAL 0)
    # Do not leave a failed download behind: it would be include()d on the next configure.
    file(REMOVE ${CPM_DOWNLOAD_LOCATION})
    message(FATAL_ERROR "Failed to download CPM.cmake: ${download_status_message}")
  endif()
endfunction()

# The cached file is executed by include() below, so verify its integrity even on the cache path
# and re-download when it is missing, truncated, or fails the hash check.
set(cpm_file_ok FALSE)
if(EXISTS ${CPM_DOWNLOAD_LOCATION})
  file(SHA256 ${CPM_DOWNLOAD_LOCATION} cpm_actual_hash)
  if("SHA256=${cpm_actual_hash}" STREQUAL "${CPM_DOWNLOAD_HASH}")
    set(cpm_file_ok TRUE)
  endif()
  unset(cpm_actual_hash)
endif()
if(NOT cpm_file_ok)
  download_cpm()
endif()
unset(cpm_file_ok)

include(${CPM_DOWNLOAD_LOCATION})
