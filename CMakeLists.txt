cmake_minimum_required(VERSION 3.16)
project(kvstore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(kvstore
    src/main.cpp
    src/kvstore.cpp
    src/repl.cpp
    src/server.cpp
)

target_include_directories(kvstore PRIVATE include)
target_link_libraries(kvstore PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(kvstore PRIVATE /W4)
else()
    target_compile_options(kvstore PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(kvstore_tests
    tests/tests.cpp
    src/kvstore.cpp
    src/repl.cpp
    src/server.cpp
)

target_include_directories(kvstore_tests PRIVATE include)
target_link_libraries(kvstore_tests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(kvstore_tests PRIVATE /W4)
else()
    target_compile_options(kvstore_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME kvstore_tests COMMAND kvstore_tests)

add_executable(kvstore_bench
    bench/bench.cpp
    src/kvstore.cpp
)

target_include_directories(kvstore_bench PRIVATE include)
target_link_libraries(kvstore_bench PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(kvstore_bench PRIVATE /W4)
else()
    target_compile_options(kvstore_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()
