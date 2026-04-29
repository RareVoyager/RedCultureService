#include <boost/version.hpp>
#include <fmt/core.h>
#include <gtest/gtest.h>
#include <hiredis/hiredis.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>
#include <spdlog/spdlog.h>
#include <sw/redis++/redis++.h>

#include <google/protobuf/stubs/common.h>
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>
#include <prometheus/registry.h>
#include <pqxx/pqxx>
#include <yaml-cpp/yaml.h>
#include <zlib.h>

#include <iostream>
#include <string>

#ifdef RCS_WITH_GRPC
#include <grpcpp/grpcpp.h>
#endif

namespace {

void print_ok(const std::string& name, const std::string& detail)
{
    std::cout << "[OK] " << name << ": " << detail << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    // 统一验证所有 Conan 三方库是否可以被 CMake 找到、编译并链接。
    ::testing::InitGoogleTest(&argc, argv);
    spdlog::info("3rdpatryexample unified smoke test start");

    nlohmann::json json_doc;
    json_doc["service"] = "RedCultureService";
    json_doc["ok"] = true;

    prometheus::Registry registry;
    YAML::Node node;
    node["module"] = "example";

    const auto jwt_token = jwt::create()
                               .set_issuer("rcs")
                               .sign(jwt::algorithm::none{});

    print_ok("boost", BOOST_LIB_VERSION);
    print_ok("spdlog", "logger initialized");
    print_ok("fmt", fmt::format("fmt {}", "loaded"));
    print_ok("nlohmann_json", json_doc.dump());
    print_ok("yaml-cpp", node["module"].as<std::string>());
    print_ok("openssl", OpenSSL_version(OPENSSL_VERSION));
    print_ok("protobuf", std::to_string(GOOGLE_PROTOBUF_VERSION));
    print_ok("redis-plus-plus", "headers loaded");
    print_ok("hiredis", std::to_string(HIREDIS_MAJOR) + "." + std::to_string(HIREDIS_MINOR));
    print_ok("libpqxx", PQXX_VERSION);
    print_ok("zlib", zlibVersion());
    print_ok("prometheus-cpp", "registry constructed");
    print_ok("jwt-cpp", jwt_token.empty() ? "empty token" : "token created");
    print_ok("gtest", "framework initialized");

#ifdef RCS_WITH_GRPC
    print_ok("grpc", grpc::Version());
#else
    print_ok("grpc", "disabled by RCS_WITH_GRPC");
#endif

    return 0;
}
