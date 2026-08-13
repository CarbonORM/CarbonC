cmake_minimum_required(VERSION 3.16)

project(CarbonC
        VERSION 0.1.0
        DESCRIPTION "Portable CarbonORM kernel"
        LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(carbonc src/carbon.c)
target_include_directories(carbonc PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(carbonc PRIVATE -Wall -Wextra -Wpedantic)
endif ()

enable_testing()

add_executable(carbonc_tests tests/test_carbon.c)
target_link_libraries(carbonc_tests PRIVATE carbonc)
add_test(NAME carbonc_tests COMMAND carbonc_tests)
