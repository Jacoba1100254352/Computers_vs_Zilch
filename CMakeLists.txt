cmake_minimum_required(VERSION 3.17)
project(ComputersVsZilch)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(CTest)

add_executable(zilch
    main.cpp
    zilch.cpp
    computer.cpp
    InputClosed.h)

target_link_libraries(zilch PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(zilch PRIVATE /W4 /permissive-)
else()
    target_compile_options(zilch PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(zilch_tests
        tests.cpp
        zilch.cpp
        computer.cpp
        InputClosed.h)
    target_link_libraries(zilch_tests PRIVATE Threads::Threads)

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
