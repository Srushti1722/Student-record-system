cmake_minimum_required(VERSION 3.16)

# Pick up vcpkg automatically so you don't pass -DCMAKE_TOOLCHAIN_FILE every time.
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
elseif(NOT CMAKE_TOOLCHAIN_FILE AND EXISTS "D:/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
endif()

project(StudentRecordSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Boost 1.70 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

add_library(student_store INTERFACE)
target_include_directories(student_store INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(student_server src/server.cpp)
target_link_libraries(student_server PRIVATE student_store Boost::system Threads::Threads)

add_executable(student_client src/client.cpp)
target_link_libraries(student_client PRIVATE student_store Boost::system Threads::Threads)

add_executable(student_store_tests tests/store_tests.cpp)
target_link_libraries(student_store_tests PRIVATE student_store)

add_executable(student_demo demo.cpp)
target_link_libraries(student_demo PRIVATE student_store)

if (WIN32)
    add_executable(student_http_server web_server.cpp)
    target_link_libraries(student_http_server PRIVATE student_store ws2_32 Threads::Threads)
endif()

enable_testing()
add_test(NAME store_tests COMMAND student_store_tests)
