cmake_minimum_required(VERSION 3.14)
project(ConcurrentTaskScheduler CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(Threads REQUIRED)

# --- Benchmark executable -----------------------------------------------
add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE Threads::Threads)

# --- Tests (GoogleTest) ---------------------------------------------------
find_package(GTest QUIET)

if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
add_executable(run_tests test_thread_pool.cpp)
target_link_libraries(run_tests PRIVATE Threads::Threads)

if(GTest_FOUND)
  target_link_libraries(run_tests PRIVATE GTest::gtest)
else()
  target_link_libraries(run_tests PRIVATE gtest)
endif()

include(GoogleTest)
gtest_discover_tests(run_tests)
