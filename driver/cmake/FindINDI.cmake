# Locate the headers and driver library installed by libindi-dev.
#
# Optional hint:
#   cmake -DINDI_ROOT=/custom/indi/prefix ..
#
# Result variables:
#   INDI_FOUND
#   INDI_VERSION
#   INDI_INCLUDE_DIR
#   INDI_DRIVER_LIBRARY
#   INDI::Driver

include(FindPackageHandleStandardArgs)

set(_INDI_HINTS)
if(INDI_ROOT)
    list(APPEND _INDI_HINTS "${INDI_ROOT}")
endif()
if(DEFINED ENV{INDI_ROOT})
    list(APPEND _INDI_HINTS "$ENV{INDI_ROOT}")
endif()

find_path(INDI_INCLUDE_DIR
    NAMES indiversion.h defaultdevice.h
    HINTS ${_INDI_HINTS}
    PATH_SUFFIXES include/libindi libindi
)

find_library(INDI_DRIVER_LIBRARY
    NAMES indidriver
    HINTS ${_INDI_HINTS}
    PATH_SUFFIXES lib lib64
)

if(INDI_INCLUDE_DIR AND EXISTS "${INDI_INCLUDE_DIR}/indiversion.h")
    file(STRINGS "${INDI_INCLUDE_DIR}/indiversion.h" _INDI_VERSION_LINE
         REGEX "^#define[ \t]+INDI_VERSION[ \t]+\"[0-9]+\\.[0-9]+\\.[0-9]+\"")
    if(_INDI_VERSION_LINE MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
        set(INDI_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

find_package_handle_standard_args(INDI
    REQUIRED_VARS INDI_INCLUDE_DIR INDI_DRIVER_LIBRARY
    VERSION_VAR INDI_VERSION
)

if(INDI_FOUND AND NOT TARGET INDI::Driver)
    add_library(INDI::Driver UNKNOWN IMPORTED)
    set_target_properties(INDI::Driver PROPERTIES
        IMPORTED_LOCATION "${INDI_DRIVER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${INDI_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(INDI_INCLUDE_DIR INDI_DRIVER_LIBRARY)
