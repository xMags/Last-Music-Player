# Private build-time values.
#
# Values that must not be published (service origins, client ids, opaque tags)
# are read from build/private-config.local.cmake, which git ignores. Without
# that file every value is empty and the features that need them stay off.
#
# To add a value: append its name to LMP_PRIVATE_VALUE_NAMES, add a matching
# line to cmake/PrivateConfigValues.h.in, and expose it through
# src/core/config/PrivateConfig.

set(LMP_PRIVATE_VALUE_NAMES
    LMP_ACCOUNT_API_ORIGIN
    LMP_ACCOUNT_FRONTEND_ORIGIN
    LMP_ACCOUNT_MEDIA_ORIGIN
    LMP_DISCORD_CLIENT_ID
    LMP_SOURCE_TAG_CATALOG
    LMP_SOURCE_TAGS_STREAM
    LMP_SOURCE_LABELS
    LMP_EXTERNAL_PLAYLIST_ID_PREFIX)

# Names in this list must hold an https origin when they are not empty.
set(LMP_PRIVATE_ORIGIN_NAMES
    LMP_ACCOUNT_API_ORIGIN
    LMP_ACCOUNT_FRONTEND_ORIGIN
    LMP_ACCOUNT_MEDIA_ORIGIN)

option(LMP_ALLOW_INSECURE_PRIVATE_ORIGINS
    "Accept http origins in the private config (local development only)" OFF)

foreach(name IN LISTS LMP_PRIVATE_VALUE_NAMES)
    set(${name} "")
endforeach()

set(_lmp_private_file "${CMAKE_SOURCE_DIR}/build/private-config.local.cmake")
if(EXISTS "${_lmp_private_file}")
    include("${_lmp_private_file}")
    message(STATUS "Private config: loaded")
else()
    message(STATUS "Private config: not present, private values are empty")
endif()

foreach(name IN LISTS LMP_PRIVATE_VALUE_NAMES)
    # The values are pasted into C++ string literals.
    if("${${name}}" MATCHES "[\"\\\\\n\r]")
        message(FATAL_ERROR "${name} contains a quote, backslash or line break")
    endif()
endforeach()

if(LMP_ACCOUNT_API_ORIGIN STREQUAL ""
   AND (NOT LMP_ACCOUNT_FRONTEND_ORIGIN STREQUAL "" OR NOT LMP_ACCOUNT_MEDIA_ORIGIN STREQUAL ""))
    message(FATAL_ERROR
        "LMP_ACCOUNT_API_ORIGIN is required when a frontend or media origin is set")
endif()

foreach(name IN LISTS LMP_PRIVATE_ORIGIN_NAMES)
    set(_value "${${name}}")
    if(_value STREQUAL "")
        continue()
    endif()
    if(_value MATCHES "/$")
        message(FATAL_ERROR "${name} must not end with a slash")
    endif()
    if(NOT _value MATCHES "^https://" AND NOT LMP_ALLOW_INSECURE_PRIVATE_ORIGINS)
        message(FATAL_ERROR "${name} must start with https://")
    endif()
endforeach()

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/PrivateConfigValues.h.in"
    "${CMAKE_BINARY_DIR}/generated/PrivateConfigValues.h"
    @ONLY)
