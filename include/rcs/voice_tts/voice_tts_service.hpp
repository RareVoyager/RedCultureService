#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::voice_tts {

using TtsTaskId = std::uint64_t;
using AudioBytes = std::vector<std::uint8_t>;

enum class AudioFormat {
    wav = 0,
    mp3 = 1,
    pcm16 = 2,
    ogg = 3,
};

enum class TtsTaskStatus {
    queued = 0,
    running = 1,
    succeeded = 2,
    failed = 3,
    timed_out = 4,
    canceled = 5,
};

struct VoiceProfile {
    std::string voice_id{"default"};
    std::string language{"zh-CN"};
    float speed{1.0F};
    float pitch{1.0F};
    float volume{1.0F};
};

struct TtsRequest {
    std::string text;
    VoiceProfile voice;
    AudioFormat format{AudioFormat::mp3};
    std::string player_id;
    std::string purpose{"ai_explanation"};
    bool cache_enabled{true};
    std::unordered_map<std::string, std::string> metadata;
};

struct AudioResource {
    std::string id;
    AudioFormat format{AudioFormat::mp3};
    std::string mime_type{"audio/mpeg"};
    AudioBytes bytes;
    std::chrono::milliseconds duration{0};
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point expires_at{};
    std::unordered_map<std::string, std::string> metadata;
};

struct TtsProviderRequest {
    TtsTaskId task_id{0};
    TtsRequest request;
    std::uint32_t attempt{0};
};

struct TtsProviderResponse {
    bool ok{false};
    AudioFormat format{AudioFormat::mp3};
    std::string mime_type{"audio/mpeg"};
    AudioBytes bytes;
    std::chrono::milliseconds duration{0};
    std::string error;
    std::unordered_map<std::string, std::string> metadata;
};

struct TtsTask {
    TtsTaskId id{0};
    TtsTaskStatus status{TtsTaskStatus::queued};
    TtsRequest request;
    std::uint32_t attempts{0};
    std::string cache_key;
    std::string last_error;
    std::optional<AudioResource> audio;
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point updated_at{};
    std::chrono::steady_clock::time_point next_attempt_at{};
};

struct VoiceTtsConfig {
    std::uint32_t max_retries{2};
    std::chrono::milliseconds request_timeout{std::chrono::seconds(8)};
    std::chrono::milliseconds retry_backoff{std::chrono::milliseconds(500)};
    std::size_t max_text_length{1200};
    std::size_t max_queued_tasks{512};
    std::size_t max_cache_items{256};
    std::chrono::seconds cache_ttl{std::chrono::minutes(30)};
};

struct SubmitResult {
    bool ok{false};
    std::string error;
    bool cache_hit{false};
    std::optional<TtsTask> task;
};

struct TickResult {
    std::vector<TtsTask> succeeded_tasks;
    std::vector<TtsTask> retry_tasks;
    std::vector<TtsTask> failed_tasks;
};

class ITtsClient {
public:
    virtual ~ITtsClient() = default;

    // 具体 TTS 调用由 C++ 实现类完成，可以是 HTTP、gRPC 或本地 mock。
    virtual TtsProviderResponse synthesize(const TtsProviderRequest& request) = 0;
};

class VoiceTtsService {
public:
    explicit VoiceTtsService(std::shared_ptr<ITtsClient> client = nullptr,
                             VoiceTtsConfig config = {});

    const VoiceTtsConfig& config() const noexcept;
    void set_client(std::shared_ptr<ITtsClient> client);

    // 提交文本转语音任务。命中缓存时会直接返回 succeeded 任务。
    SubmitResult submit(TtsRequest request);
    bool cancel_task(TtsTaskId task_id);

    // 推进队列中的任务，执行实际 TTS 调用、重试和超时判断。
    TickResult tick(std::size_t max_tasks = 1);

    std::optional<TtsTask> find_task(TtsTaskId task_id) const;
    std::optional<AudioResource> find_audio(const std::string& audio_id) const;
    std::vector<TtsTask> list_tasks() const;

    std::size_t queued_task_count() const;
    std::size_t cache_size() const;
    void clear_expired_cache();

private:
    struct RunningTask {
        TtsTask task;
        TtsProviderRequest request;
    };

    SubmitResult reject(std::string error) const;
    std::string build_cache_key(const TtsRequest& request) const;
    std::string build_audio_id(const std::string& cache_key, TtsTaskId task_id) const;
    std::optional<AudioResource> find_cached_audio_locked(const std::string& cache_key,
                                                          std::chrono::steady_clock::time_point now) const;
    void store_cache_locked(const std::string& cache_key, const AudioResource& audio);
    void enforce_cache_limit_locked();
    std::optional<RunningTask> take_next_task_locked(std::chrono::steady_clock::time_point now);
    TtsTask finish_task_locked(const RunningTask& running,
                               TtsProviderResponse response,
                               std::chrono::milliseconds elapsed,
                               TickResult& result);

    VoiceTtsConfig config_;
    std::shared_ptr<ITtsClient> client_;
    mutable std::mutex mutex_;
    TtsTaskId next_task_id_{1};
    std::unordered_map<TtsTaskId, TtsTask> tasks_;
    std::unordered_map<std::string, AudioResource> audio_cache_;
    std::unordered_map<std::string, std::string> audio_id_to_cache_key_;
};

const char* to_string(AudioFormat format);
const char* to_string(TtsTaskStatus status);
const char* mime_type(AudioFormat format);

} // namespace rcs::voice_tts
