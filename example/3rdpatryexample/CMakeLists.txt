# 该目录只生成一个统一 smoke test 可执行文件，避免每个三方库都生成一个 target。

set(RCS_REDIS_TARGET "")
if(TARGET redis-plus-plus::redis-plus-plus)
    set(RCS_REDIS_TARGET redis-plus-plus::redis-plus-plus)
elseif(TARGET redis++::redis++)
    set(RCS_REDIS_TARGET redis++::redis++)
elseif(TARGET redis++::redis++_static)
    set(RCS_REDIS_TARGET redis++::redis++_static)
endif()

if("${RCS_REDIS_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No redis-plus-plus CMake target found")
endif()

set(RCS_HIREDIS_TARGET "")
if(TARGET hiredis::hiredis)
    set(RCS_HIREDIS_TARGET hiredis::hiredis)
elseif(TARGET hiredis::libhiredis)
    set(RCS_HIREDIS_TARGET hiredis::libhiredis)
endif()

if("${RCS_HIREDIS_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No hiredis CMake target found")
endif()

set(RCS_PROMETHEUS_TARGET "")
if(TARGET prometheus-cpp::core)
    set(RCS_PROMETHEUS_TARGET prometheus-cpp::core)
elseif(TARGET prometheus-cpp::prometheus-cpp)
    set(RCS_PROMETHEUS_TARGET prometheus-cpp::prometheus-cpp)
endif()

if("${RCS_PROMETHEUS_TARGET}" STREQUAL "")
    message(FATAL_ERROR "No prometheus-cpp CMake target found")
endif()

add_executable(rcs_3rdpatryexample
    main.cpp
)

target_link_libraries(rcs_3rdpatryexample
    PRIVATE
        RedCulture::service
        ${RCS_BOOST_TARGET}
        spdlog::spdlog_header_only
        fmt::fmt
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        OpenSSL::SSL
        OpenSSL::Crypto
        protobuf::libprotobuf
        ${RCS_REDIS_TARGET}
        ${RCS_HIREDIS_TARGET}
        ${RCS_PQXX_TARGET}
        ZLIB::ZLIB
        ${RCS_PROMETHEUS_TARGET}
        jwt-cpp::jwt-cpp
        GTest::gtest
)

if(RCS_WITH_GRPC)
    target_link_libraries(rcs_3rdpatryexample
        PRIVATE
            gRPC::grpc++
    )
endif()
