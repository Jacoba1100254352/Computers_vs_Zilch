cmake_minimum_required(VERSION 3.17)
project(ComputersVsZilch)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(CTest)

set(ZILCH_CORE_SOURCES
    zilch.cpp
    computer.cpp)

add_executable(zilch
    main.cpp
    ${ZILCH_CORE_SOURCES}
    InputClosed.h)

target_include_directories(zilch PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(zilch PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(zilch PRIVATE /W4 /permissive-)
else()
    target_compile_options(zilch PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(zilch_tests
        tests/regression_tests.cpp
        ${ZILCH_CORE_SOURCES}
        InputClosed.h)
    target_include_directories(zilch_tests PRIVATE ${PROJECT_SOURCE_DIR})
    target_link_libraries(zilch_tests PRIVATE Threads::Threads)
    target_compile_definitions(zilch_tests PRIVATE ZILCH_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    if(MSVC)
        target_compile_options(zilch_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(zilch_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME zilch_tests COMMAND zilch_tests)
    add_test(
        NAME zilch_cli_regression
        COMMAND /bin/sh
                ${CMAKE_CURRENT_SOURCE_DIR}/cli_regression.sh
                $<TARGET_FILE:zilch>
                ${CMAKE_CURRENT_SOURCE_DIR})
endif()
