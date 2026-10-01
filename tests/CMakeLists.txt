find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(tests test_xbee_frame.cpp test_payload_codec.cpp test_integration.cpp)
target_link_libraries(tests cansat_comms GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tests)
