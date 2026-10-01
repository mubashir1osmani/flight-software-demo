cmake_minimum_required(VERSION 3.14)
project(cansat_comms CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "build tests" ON)
set(CANSAT_XBEE_MAX_PAYLOAD 100 CACHE STRING "max xbee payload")

add_library(cansat_comms src/xbee_frame.cpp src/payload_codec.cpp)
target_include_directories(cansat_comms PUBLIC include)
target_compile_definitions(cansat_comms PUBLIC CANSAT_XBEE_MAX_PAYLOAD=${CANSAT_XBEE_MAX_PAYLOAD})

# no exceptions/rtti since this runs on the mcu
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cansat_comms PRIVATE -Wall -Wextra -Wconversion -fno-exceptions -fno-rtti)
endif()

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
