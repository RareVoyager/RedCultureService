cmake_minimum_required(VERSION 3.22)

# Handle Conan target naming differences across recipes/versions.
set(RCS_REDIS_TARGET "")
if(TARGET redis-plus-plus::redis-plus-plus)
    set(RCS_REDIS_TARGET redis-plus-plus::redis-plus-plus)
elseif(TARGET redis++::redis++)
    set(RCS_REDIS_TARGET redis++::redis++)
elseif(TARGET redis++::redis++_static)
    set(RCS_REDIS_TARGET redis++::redis++_static)
endif()

set(RCS_HIREDIS_TARGET "")
if(TARGET hiredis::hiredis)
    set(RCS_HIREDIS_TARGET hiredis::hiredis)
elseif(TARGET hiredis::libhiredis)
    set(RCS_HIREDIS_TARGET hiredis::libhiredis)
endif()

set(RCS_PQXX_TARGET "")
if(TARGET libpqxx::libpqxx)
    set(RCS_PQXX_TARGET libpqxx::libpqxx)
elseif(TARGET libpqxx::pqxx)
    set(RCS_PQXX_TARGET libpqxx::pqxx)
elseif(TARGET pqxx::pqxx)
    set(RCS_PQXX_TARGET pqxx::pqxx)
endif()

function(rcs_add_3rdparty_example target file)
    add_executable(${target} ${file})
    target_link_libraries(${target} PRIVATE RedCulture::service)
endfunction()

rcs_add_3rdparty_example(test_boost test_boost.cpp)
target_link_libraries(test_boost PRIVATE ${RCS_BOOST_TARGET})

rcs_add_3rdparty_example(test_spdlog test_spdlog.cpp)
target_link_libraries(test_spdlog PRIVATE spdlog::spdlog_header_only)

rcs_add_3rdparty_example(test_fmt test_fmt.cpp)
target_link_libraries(test_fmt PRIVATE fmt::fmt)

rcs_add_3rdparty_example(test_nlohmann_json test_nlohmann_json.cpp)
target_link_libraries(test_nlohmann_json PRIVATE nlohmann_json::nlohmann_json)

rcs_add_3rdparty_example(test_yaml_cpp test_yaml_cpp.cpp)
target_link_libraries(test_yaml_cpp PRIVATE yaml-cpp::yaml-cpp)

rcs_add_3rdparty_example(test_openssl test_openssl.cpp)
target_link_libraries(test_openssl PRIVATE OpenSSL::SSL OpenSSL::Crypto)

rcs_add_3rdparty_example(test_protobuf test_protobuf.cpp)
target_link_libraries(test_protobuf PRIVATE protobuf::libprotobuf)

if(RCS_WITH_GRPC)
    rcs_add_3rdparty_example(test_grpc test_grpc.cpp)
    target_link_libraries(test_grpc PRIVATE gRPC::grpc++)
endif()

rcs_add_3rdparty_example(test_redis_plus_plus test_redis_plus_plus.cpp)
if("${RCS_REDIS_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No redis-plus-plus CMake target found")
endif()
if("${RCS_HIREDIS_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No hiredis CMake target found")
endif()
target_link_libraries(test_redis_plus_plus PRIVATE ${RCS_REDIS_TARGET} ${RCS_HIREDIS_TARGET})

rcs_add_3rdparty_example(test_hiredis test_hiredis.cpp)
target_link_libraries(test_hiredis PRIVATE ${RCS_HIREDIS_TARGET})

rcs_add_3rdparty_example(test_libpqxx test_libpqxx.cpp)
if("${RCS_PQXX_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No libpqxx CMake target found")
endif()
target_link_libraries(test_libpqxx PRIVATE ${RCS_PQXX_TARGET})

rcs_add_3rdparty_example(test_zlib test_zlib.cpp)
target_link_libraries(test_zlib PRIVATE ZLIB::ZLIB)

rcs_add_3rdparty_example(test_prometheus_cpp test_prometheus_cpp.cpp)
target_link_libraries(test_prometheus_cpp PRIVATE prometheus-cpp::core)

rcs_add_3rdparty_example(test_jwt_cpp test_jwt_cpp.cpp)
target_link_libraries(test_jwt_cpp PRIVATE jwt-cpp::jwt-cpp)

rcs_add_3rdparty_example(test_gtest test_gtest.cpp)
target_link_libraries(test_gtest PRIVATE GTest::gtest GTest::gtest_main)
