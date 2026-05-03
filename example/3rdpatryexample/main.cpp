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

void printOk(const std::string& name, const std::string& detail)
{
    std::cout << "[OK] " << name << ": " << detail << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    // 统一验证 Conan 三方库是否能被 CMake 找到、编译并链接。
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

    printOk("boost", BOOST_LIB_VERSION);
    printOk("spdlog", "logger initialized");
    printOk("fmt", fmt::format("fmt {}", "loaded"));
    printOk("nlohmann_json", json_doc.dump());
    printOk("yaml-cpp", node["module"].as<std::string>());
    printOk("openssl", OpenSSL_version(OPENSSL_VERSION));
    printOk("protobuf", std::to_string(GOOGLE_PROTOBUF_VERSION));
    printOk("redis-plus-plus", "headers loaded");
    printOk("hiredis", std::to_string(HIREDIS_MAJOR) + "." + std::to_string(HIREDIS_MINOR));
    printOk("libpqxx", PQXX_VERSION);
    printOk("zlib", zlibVersion());
    printOk("prometheus-cpp", "registry constructed");
    printOk("jwt-cpp", jwt_token.empty() ? "empty token" : "token created");
    printOk("gtest", "framework initialized");

#ifdef RCS_WITH_GRPC
    printOk("grpc", grpc::Version());
#else
    printOk("grpc", "disabled by RCS_WITH_GRPC");
#endif

    return 0;
}
