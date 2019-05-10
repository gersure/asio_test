cmake_minimum_required(VERSION 3.14)
project(asio_test)

add_compile_options(-g)

find_package(Boost REQUIRED )
include_directories(${Boost_INCLUDE_DIRS})
set(EXTERN_LIBS ${Boost_LIBRARIES} boost_system pthread)

set(CMAKE_CXX_STANDARD 11)

set(common_source
        handle_allocation.h
        io_pool.h
        io_pool.cpp
        )


function(add_test file)
    STRING(REGEX REPLACE ".+/(.+)\\..*" "\\1" file_name ${file})
    add_executable(${file_name} ${file} ${common_source})
    target_link_libraries(${file_name} ${EXTERN_LIBS})
endfunction()

FILE(GLOB SRC_FILES server.cpp client.cpp)
FOREACH(FILE_PATH ${SRC_FILES})
    MESSAGE(${FILE_PATH})
    add_test(${FILE_PATH})
ENDFOREACH(FILE_PATH)
