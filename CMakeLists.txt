cmake_minimum_required(VERSION 3.16)
project(quantus_ascend_miner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(quantus_ascend_miner
    host/main.cpp
    host/stratum_client.cpp
    host/poseidon2.cpp
)
target_include_directories(quantus_ascend_miner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/host
)
# May need -lpthread on some systems
find_package(Threads REQUIRED)
target_link_libraries(quantus_ascend_miner PRIVATE Threads::Threads)

install(TARGETS quantus_ascend_miner RUNTIME DESTINATION bin)
