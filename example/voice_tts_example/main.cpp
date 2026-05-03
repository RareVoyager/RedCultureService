#include "rcs/voice_tts/voice_tts_service.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

class MockTtsClient final : public rcs::voice_tts::ITtsClient {
public:
    rcs::voice_tts::TtsProviderResponse synthesize(const rcs::voice_tts::TtsProviderRequest& request) override {
        const std::string text = "FAKE_MP3_AUDIO:" + request.request.text;
        rcs::voice_tts::AudioBytes bytes(text.begin(), text.end());

        rcs::voice_tts::TtsProviderResponse response;
        response.ok = true;
        response.format = request.request.format;
        response.mime_type = rcs::voice_tts::mimeType(request.request.format);
        response.bytes = std::move(bytes);
        response.duration = std::chrono::milliseconds(1800);
        response.metadata["mock"] = "true";
        response.metadata["voice"] = request.request.voice.voice_id;
        return response;
    }
};

void printTask(const rcs::voice_tts::TtsTask& task) {
    std::cout << "task id: " << task.id
              << ", status: " << rcs::voice_tts::toString(task.status)
              << ", attempts: " << task.attempts
              << '\n';

    if (task.audio) {
        std::cout << "audio id: " << task.audio->id
                  << ", mime: " << task.audio->mime_type
                  << ", bytes: " << task.audio->bytes.size()
                  << '\n';
    }
}

} // namespace

int main() {
    auto client = std::make_shared<MockTtsClient>();
    rcs::voice_tts::VoiceTtsService service(client);

    rcs::voice_tts::TtsRequest request;
    request.text = "欢迎来到红色文化展厅，请认真观察场景线索。";
    request.player_id = "player-1";
    request.voice.voice_id = "zh-CN-narrator";
    request.format = rcs::voice_tts::AudioFormat::mp3;

    auto submit = service.submit(request);
    if (!submit.ok || !submit.task) {
        std::cout << "submit failed: " << submit.error << '\n';
        return 1;
    }

    auto tick = service.tick();
    if (!tick.succeeded_tasks.empty()) {
        printTask(tick.succeeded_tasks.front());
    }

    auto cached = service.submit(request);
    if (cached.cache_hit && cached.task) {
        std::cout << "cache hit\n";
        printTask(*cached.task);
    }

    std::cout << "cache size: " << service.cacheSize() << '\n';
    return 0;
}
