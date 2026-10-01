set(_INDI_FLATPAK_DRIVER_LINK "/app/lib/libindidriver.so")

if(NOT EXISTS "${_INDI_FLATPAK_DRIVER_LINK}")
    message(FATAL_ERROR
        "KStars' INDI runtime was not found at ${_INDI_FLATPAK_DRIVER_LINK}. "
        "Run this configuration inside KStars Flatpak with --devel.")
endif()

if(NOT INDI_SOURCE_ROOT)
    message(FATAL_ERROR
        "INDI_SOURCE_ROOT is required in Flatpak mode. Point it at the INDI "
        "source tag matching KStars' bundled libindidriver.")
endif()

get_filename_component(INDI_SOURCE_ROOT "${INDI_SOURCE_ROOT}" ABSOLUTE)
if(NOT EXISTS "${INDI_SOURCE_ROOT}/libs/indibase/defaultdevice.h")
    message(FATAL_ERROR
        "${INDI_SOURCE_ROOT} is not a complete INDI source tree.")
endif()

get_filename_component(_INDI_FLATPAK_DRIVER_REAL
                       "${_INDI_FLATPAK_DRIVER_LINK}" REALPATH)
get_filename_component(_INDI_FLATPAK_DRIVER_NAME
                       "${_INDI_FLATPAK_DRIVER_REAL}" NAME)
if(NOT _INDI_FLATPAK_DRIVER_NAME MATCHES
   "^libindidriver\\.so\\.([0-9]+\\.[0-9]+\\.[0-9]+)$")
    message(FATAL_ERROR
        "Could not determine INDI version from ${_INDI_FLATPAK_DRIVER_REAL}.")
endif()
set(INDI_VERSION "${CMAKE_MATCH_1}")

file(STRINGS "${INDI_SOURCE_ROOT}/CMakeLists.txt" _INDI_SOURCE_MAJOR_LINE
     REGEX "^set\\(CMAKE_INDI_VERSION_MAJOR [0-9]+\\)")
file(STRINGS "${INDI_SOURCE_ROOT}/CMakeLists.txt" _INDI_SOURCE_MINOR_LINE
     REGEX "^set\\(CMAKE_INDI_VERSION_MINOR [0-9]+\\)")
file(STRINGS "${INDI_SOURCE_ROOT}/CMakeLists.txt" _INDI_SOURCE_RELEASE_LINE
     REGEX "^set\\(CMAKE_INDI_VERSION_RELEASE [0-9]+\\)")
string(REGEX MATCH "[0-9]+" _INDI_SOURCE_MAJOR "${_INDI_SOURCE_MAJOR_LINE}")
string(REGEX MATCH "[0-9]+" _INDI_SOURCE_MINOR "${_INDI_SOURCE_MINOR_LINE}")
string(REGEX MATCH "[0-9]+" _INDI_SOURCE_RELEASE "${_INDI_SOURCE_RELEASE_LINE}")
set(_INDI_SOURCE_VERSION
    "${_INDI_SOURCE_MAJOR}.${_INDI_SOURCE_MINOR}.${_INDI_SOURCE_RELEASE}")

if(NOT _INDI_SOURCE_VERSION VERSION_EQUAL INDI_VERSION)
    message(FATAL_ERROR
        "INDI source ${_INDI_SOURCE_VERSION} does not match KStars Flatpak "
        "runtime ${INDI_VERSION}.")
endif()

set(_INDI_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/indi-generated")
file(MAKE_DIRECTORY "${_INDI_GENERATED_INCLUDE}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/indiversion.h.in"
    "${_INDI_GENERATED_INCLUDE}/indiversion.h"
    @ONLY
)

add_library(INDI::Driver UNKNOWN IMPORTED)
set_target_properties(INDI::Driver PROPERTIES
    IMPORTED_LOCATION "${_INDI_FLATPAK_DRIVER_LINK}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${_INDI_GENERATED_INCLUDE};${INDI_SOURCE_ROOT}/libs;${INDI_SOURCE_ROOT}/libs/indibase;${INDI_SOURCE_ROOT}/libs/indibase/timer;${INDI_SOURCE_ROOT}/libs/indicore;${INDI_SOURCE_ROOT}/libs/indidevice;${INDI_SOURCE_ROOT}/libs/indidevice/property;${INDI_SOURCE_ROOT}/libs/eventloop"
)

# The executable is launched inside KStars' sandbox, where the matching
# runtime library is always mounted at /app/lib.
set(CMAKE_BUILD_RPATH "/app/lib")
set(CMAKE_INSTALL_RPATH "/app/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

message(STATUS "Using KStars Flatpak INDI ${INDI_VERSION}")
message(STATUS "Using matching headers from ${INDI_SOURCE_ROOT}")
