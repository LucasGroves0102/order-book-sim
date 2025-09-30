cmake_minimum_required(VERSION 3.20)
project(order_book_sim LANGUAGES CXX)

# ---------- General C++ settings ----------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default to Release if not specified (you can override with -DCMAKE_BUILD_TYPE=Debug)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Perf / warnings (tweak as you like)
add_compile_options(-O3 -march=native -flto -Wall -Wextra -Wpedantic)

include(FetchContent)

# ---------- Python (for Python.h) ----------
# Make sure you have python dev headers installed (e.g., sudo apt-get install python3-dev python3.11-dev)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# ---------- pybind11 (via FetchContent) ----------
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.12.0
)
FetchContent_MakeAvailable(pybind11)  # provides pybind11_add_module, targets, includes

# ---------- Core C++ library (engine) ----------
add_library(oblib
  src/ob/book.cpp
  src/ob/order.cpp         # keep this file present; can be an empty stub
)
target_include_directories(oblib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# (Optional) If oblib ever needs Python includes:
target_include_directories(oblib PRIVATE
  ${Python3_INCLUDE_DIRS}
)

# ---------- Python extension module ----------
# Produces a Python-importable module named "obsim"
pybind11_add_module(obsim
  src/bindings/py_module.cpp
)
target_link_libraries(obsim PRIVATE oblib)
# (Optional) Ensure module sees Python headers (pybind11 usually handles it)
target_include_directories(obsim PRIVATE
  ${Python3_INCLUDE_DIRS}
)

# ---------- GoogleTest for C++ tests ----------
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(test_book
  tests/cpp/test_book.cpp
)
target_link_libraries(test_book PRIVATE oblib gtest_main)
target_include_directories(test_book PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(test_book)

# ---------- Helpful output ----------
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Python include dirs: ${Python3_INCLUDE_DIRS}")
message(STATUS "pybind11 included via FetchContent")
message(STATUS "GoogleTest included via FetchContent")

