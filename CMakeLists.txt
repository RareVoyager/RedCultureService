cmake_minimum_required(VERSION 3.22)

project(RedCultureService VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RCS_WITH_GRPC "Enable gRPC related dependencies" ON)
option(RCS_BUILD_EXAMPLES "Build example binaries" ON)
option(RCS_BUILD_TESTS "Build tests" OFF)
option(RCS_BUILD_SERVER "Build red culture HTTP server" ON)

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
find_package(Threads REQUIRED)

set(RCS_LIBXCRYPT_TARGET "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(libxcrypt CONFIG REQUIRED)

    if(TARGET libxcrypt::libxcrypt)
        set(RCS_LIBXCRYPT_TARGET libxcrypt::libxcrypt)
    elseif(TARGET libxcrypt::crypt)
        set(RCS_LIBXCRYPT_TARGET libxcrypt::crypt)
    elseif(TARGET crypt::crypt)
        set(RCS_LIBXCRYPT_TARGET crypt::crypt)
    elseif(TARGET xcrypt::xcrypt)
        set(RCS_LIBXCRYPT_TARGET xcrypt::xcrypt)
    endif()

    if("${RCS_LIBXCRYPT_TARGET}" STREQUAL "")
        message(FATAL_ERROR "No libxcrypt CMake target found")
    endif()
endif()

set(RCS_BOOST_TARGET "")
if(TARGET Boost::headers)
    set(RCS_BOOST_TARGET Boost::headers)
elseif(TARGET Boost::boost)
    set(RCS_BOOST_TARGET Boost::boost)
elseif(TARGET boost::boost)
    set(RCS_BOOST_TARGET boost::boost)
endif()

if("${RCS_BOOST_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No Boost CMake target found")
endif()

set(RCS_PQXX_TARGET "")
if(TARGET libpqxx::pqxx)
    set(RCS_PQXX_TARGET libpqxx::pqxx)
elseif(TARGET libpqxx::libpqxx)
    set(RCS_PQXX_TARGET libpqxx::libpqxx)
elseif(TARGET pqxx::pqxx)
    set(RCS_PQXX_TARGET pqxx::pqxx)
endif()

if("${RCS_PQXX_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No libpqxx CMake target found")
endif()

if(RCS_WITH_GRPC)
    find_package(gRPC CONFIG REQUIRED)
endif()

add_library(redculture_common INTERFACE)
add_library(RedCulture::common ALIAS redculture_common)

set(RCS_COMMON_HEADERS
    ${PROJECT_SOURCE_DIR}/include/rcs/common/result.hpp
)

foreach(RCS_COMMON_HEADER IN LISTS RCS_COMMON_HEADERS)
    if(NOT EXISTS ${RCS_COMMON_HEADER})
        message(FATAL_ERROR "Missing common header: ${RCS_COMMON_HEADER}")
    endif()
endforeach()

target_sources(redculture_common
    INTERFACE
        ${RCS_COMMON_HEADERS}
)

target_include_directories(redculture_common
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(redculture_common
    INTERFACE
        cxx_std_17
)

add_subdirectory(src)

if(RCS_BUILD_SERVER)
    add_subdirectory(app)
endif()

if(RCS_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(RCS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
