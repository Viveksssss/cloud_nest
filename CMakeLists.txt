cmake_minimum_required(VERSION 3.10.0)
project(cloud_nest VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 设置编译选项
if(NOT CMAKE_BUILD_TYPE)
    # set(CMAKE_BUILD_TYPE "Release")
    set(CMAKE_BUILD_TYPE "Debug")

endif()


find_package(Boost CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(muduo REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(OpenSSL REQUIRED)

# Debug模式下的编译选项
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -Werror -Wconversion -Wno-reorder -Wno-unused-parameter -Wno-shadow")
# Release模式下的编译选项
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -Wall -Wno-shadow -Wno-reorder")
# 通用编译选项
set(CXX_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wno-unused-parameter
    -Wold-style-cast
    -Woverloaded-virtual
    -Wpointer-arith
    -Wshadow
    -Wwrite-strings
    -march=native
    -std=c++17
    -rdynamic
)


# 是否构建测试
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)
endif()


message(STATUS "CXX_FLAGS = " ${CMAKE_CXX_FLAGS})
message(STATUS "CMAKE_BUILD_TYPE = " ${CMAKE_BUILD_TYPE})
message(STATUS "CMAKE_CXX_FLAGS_DEBUG = " ${CMAKE_CXX_FLAGS_DEBUG})
message(STATUS "CMAKE_CXX_FLAGS_RELEASE = " ${CMAKE_CXX_FLAGS_RELEASE}) 


# 设置输出目录
set(CMAKE_EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)


file(GLOB_RECURSE SOURCES "src/*.cc")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/static
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin
)

add_executable(cloud_nest main.cpp ${SOURCES}
    src/db/PgDatabase.cc
    src/utils/FileUtils.h
    src/utils/CodeGenerator.h
    src/handler/Router.cc)
include_directories(/usr/local/include/)
target_link_libraries(cloud_nest PRIVATE /usr/lib/libpq.so /usr/local/lib/libpqxx.a )
target_link_libraries(cloud_nest PRIVATE 
    nlohmann_json::nlohmann_json
    spdlog::spdlog 
    muduo
    OpenSSL::SSL
    OpenSSL::Crypto
)

