cmake_minimum_required(VERSION 3.22)

project(RedCultureService VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RCS_WITH_GRPC "Enable gRPC related dependencies" ON)
option(RCS_BUILD_EXAMPLES "Build example binaries" ON)
option(RCS_BUILD_TESTS "Build tests" OFF)

find_package(Boost CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenSSL CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(ZLIB CONFIG REQUIRED)
find_package(prometheus-cpp CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

if(RCS_WITH_GRPC)
    find_package(gRPC CONFIG REQUIRED)
endif()

add_subdirectory(src)

if(RCS_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(RCS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
