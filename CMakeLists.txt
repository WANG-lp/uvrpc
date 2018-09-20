cmake_minimum_required(VERSION 3.4.3)
project(uvrpc C)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH}) # To allow CMake to locate our Find*.cmake files
set(CMAKE_C_STANDARD 11)
add_compile_options(-O2)

include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if (IS_BIG_ENDIAN)
    message(STATUS "System is BIG_ENDIAN")
    add_definitions(-D_BIG_ENDIAN)
else ()
    message(STATUS "System is LITTLE_ENDIAN")
endif ()

find_package(Threads REQUIRED)
find_package(Libuv REQUIRED)
if (${LIBUV_FOUND})
    include_directories(${LIBUV_INCLUDE_DIR})
endif ()

add_library(uvrpc SHARED src/uvrpc.c src/uvrpc.h src/utils/blockQueue.h src/utils/blockQueue.c src/utils/int2bytes.h src/utils/int2bytes.c)
target_link_libraries(uvrpc ${LIBUV_LIBRARIES} Threads::Threads)

add_executable(uvrpc_server src/test/uvrpc_server_test.c src/uvrpc.h)
target_link_libraries(uvrpc_server uvrpc)

add_executable(uvrpc_client src/test/uvrpc_client_test.c src/uvrpc.h)
target_link_libraries(uvrpc_client uvrpc)

add_executable(uvrpc_server_blackhole src/test/uvrpc_server_file_blackhole.c src/uvrpc.h)
target_link_libraries(uvrpc_server_blackhole uvrpc)
add_executable(uvrpc_client_blackhole src/test/uvrpc_client_file_blackhole.c src/uvrpc.h)
target_link_libraries(uvrpc_client_blackhole uvrpc)