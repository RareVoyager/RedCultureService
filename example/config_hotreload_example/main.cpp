#include "rcs/config_hotreload/config_hotreload_service.hpp"

#include <iostream>

int main(int argc, char** argv) {
    const std::filesystem::path config_path = argc > 1 ? argv[1] : "app.yaml";

    rcs::config_hotreload::ConfigHotReloadService config_service(config_path);
    config_service.set_on_reload([](const rcs::config_hotreload::AppConfig& config) {
        std::cout << "config reloaded: "
                  << "network=" << config.network.listen_address << ':' << config.network.port
                  << ", ai_model=" << config.ai.model
                  << ", tts_voice=" << config.voice_tts.voice_id
                  << '\n';
    });

    const auto loaded = config_service.load();
    if (!loaded.ok || !loaded.config) {
        std::cout << "load config failed: " << loaded.error << '\n';
        return 1;
    }

    const auto& config = *loaded.config;
    std::cout << "postgres uri env: " << config.storage.postgres_uri_env << '\n';
    std::cout << "postgres uri fallback: " << config.storage.postgres_uri << '\n';
    std::cout << "ai provider: " << config.ai.provider << '\n';
    std::cout << "ai api key env: " << config.ai.api_key_env << '\n';
    std::cout << "hot reload enabled: " << (config.hot_reload.enabled ? "true" : "false") << '\n';

    const auto polled = config_service.poll();
    std::cout << "poll ok: " << (polled.ok ? "true" : "false") << '\n';
    return polled.ok ? 0 : 1;
}
