# Simple Makefile alternative to CMake.
# Usage:
#   make            # builds benchmark and run_tests
#   make benchmark  # builds and you can run ./benchmark
#   make test       # builds and runs the GoogleTest suite
#   make clean

CXX      := g++
CXXFLAGS := -std=c++20 -O2 -pthread -Wall -Wextra
GTEST_LIBS := -lgtest -lgtest_main -pthread

.PHONY: all test clean

all: benchmark run_tests

benchmark: benchmark.cpp thread_pool.hpp
	$(CXX) $(CXXFLAGS) benchmark.cpp -o benchmark

run_tests: test_thread_pool.cpp thread_pool.hpp
	$(CXX) $(CXXFLAGS) test_thread_pool.cpp -o run_tests $(GTEST_LIBS)

test: run_tests
	./run_tests

clean:
	rm -f benchmark run_tests
