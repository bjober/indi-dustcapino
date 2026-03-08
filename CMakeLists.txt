cmake_minimum_required(VERSION 3.10)
project(indi_dustcapino)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(indi_dustcapino
    dustcapino.cpp
)

# Include headers
target_include_directories(indi_dustcapino PRIVATE
    /usr/local/include/libindi
)

# Link explicitly against indidriver
target_link_libraries(indi_dustcapino
    indidriver
)

install(TARGETS indi_dustcapino
        RUNTIME DESTINATION bin)