#include <iostream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>
#include <openssl/ssl.h>
#include <pqxx/pqxx>
#include <sw/redis++/redis++.h>

#ifdef RCS_WITH_GRPC
#include <grpcpp/grpcpp.h>
#include <google/protobuf/message.h>
#endif

int main() {
    spdlog::info("3rdpatryexample start");

    nlohmann::json j;
    j["service"] = "RedCultureService";
    j["ok"] = true;

    YAML::Node node;
    node["module"] = "example";

    std::cout << "OpenSSL version: " << OpenSSL_version(OPENSSL_VERSION) << '\n';
    std::cout << "json: " << j.dump() << '\n';
    std::cout << "yaml module: " << node["module"].as<std::string>() << '\n';
    std::cout << "libpqxx version: " << PQXX_VERSION << '\n';
    std::cout << "redis++ headers loaded" << '\n';

#ifdef RCS_WITH_GRPC
    std::cout << "gRPC version: " << grpc::Version() << '\n';
#else
    std::cout << "gRPC disabled by RCS_WITH_GRPC" << '\n';
#endif

    return 0;
}
