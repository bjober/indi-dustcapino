cmake_minimum_required(VERSION 3.18)

project(indi_dustcapino VERSION 1.99.0 LANGUAGES CXX)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(DUSTCAPINO_KSTARS_FLATPAK
       "Build inside KStars Flatpak against its bundled INDI runtime" OFF)
set(INDI_SOURCE_ROOT "" CACHE PATH
    "Matching INDI source tree used for headers in Flatpak mode")

if(DUSTCAPINO_KSTARS_FLATPAK)
    include(cmake/FlatpakINDI.cmake)
else()
    # System-package mode remains available for distributions that provide a
    # complete libindi-dev package.
    find_package(INDI 2.2.4 REQUIRED)
endif()

add_executable(indi_dustcapino dustcapino.cpp)

target_compile_features(indi_dustcapino PRIVATE cxx_std_17)
target_compile_options(indi_dustcapino PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
target_link_libraries(indi_dustcapino PRIVATE INDI::Driver)

configure_file(indi_dustcapino.xml.in indi_dustcapino.xml @ONLY)

install(TARGETS indi_dustcapino
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/indi_dustcapino.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/indi")
