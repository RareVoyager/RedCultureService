#include "rcs/voice_tts/voice_tts_service.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>

namespace rcs::voice_tts {

namespace {

std::uint32_t max_attempts_from_retries(std::uint32_t max_retries) {
    return max_retries + 1;
}

bool is_finished(TtsTaskStatus status) {
    return status == TtsTaskStatus::succeeded ||
           status == TtsTaskStatus::failed ||
           status == TtsTaskStatus::timed_out ||
           status == TtsTaskStatus::canceled;
}

} // namespace

VoiceTtsService::VoiceTtsService(std::shared_ptr<ITtsClient> client,
                                 VoiceTtsConfig config)
    : config_(std::move(config)),
      client_(std::move(client)) {}

const VoiceTtsConfig& VoiceTtsService::config() const noexcept {
    return config_;
}

void VoiceTtsService::set_client(std::shared_ptr<ITtsClient> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = std::move(client);
}

SubmitResult VoiceTtsService::submit(TtsRequest request) {
    if (request.text.empty()) {
        return reject("text is empty");
    }
    if (request.text.size() > config_.max_text_length) {
        return reject("text is too long");
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto active_task_count = static_cast<std::size_t>(std::count_if(tasks_.begin(), tasks_.end(), [](const auto& item) {
        return item.second.status == TtsTaskStatus::queued || item.second.status == TtsTaskStatus::running;
    }));
    if (active_task_count >= config_.max_queued_tasks) {
        return reject("TTS task queue is full");
    }

    const auto now = std::chrono::steady_clock::now();
    const auto cache_key = build_cache_key(request);
    if (request.cache_enabled) {
        auto cached = find_cached_audio_locked(cache_key, now);
        if (cached) {
            TtsTask task;
            task.id = next_task_id_++;
            task.status = TtsTaskStatus::succeeded;
            task.request = std::move(request);
            task.cache_key = cache_key;
            task.audio = *cached;
            task.created_at = now;
            task.updated_at = now;
            task.next_attempt_at = now;
            tasks_[task.id] = task;
            return SubmitResult{true, {}, true, task};
        }
    }

    TtsTask task;
    task.id = next_task_id_++;
    task.status = TtsTaskStatus::queued;
    task.request = std::move(request);
    task.cache_key = cache_key;
    task.created_at = now;
    task.updated_at = now;
    task.next_attempt_at = now;
    tasks_[task.id] = task;
    return SubmitResult{true, {}, false, task};
}

bool VoiceTtsService::cancel_task(TtsTaskId task_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = tasks_.find(task_id);
    if (it == tasks_.end() || is_finished(it->second.status)) {
        return false;
    }

    it->second.status = TtsTaskStatus::canceled;
    it->second.updated_at = std::chrono::steady_clock::now();
    return true;
}

TickResult VoiceTtsService::tick(std::size_t max_tasks) {
    TickResult result;
    if (max_tasks == 0) {
        return result;
    }

    for (std::size_t i = 0; i < max_tasks; ++i) {
        std::shared_ptr<ITtsClient> client;
        RunningTask running;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto maybe_task = take_next_task_locked(std::chrono::steady_clock::now());
            if (!maybe_task) {
                break;
            }
            running = *maybe_task;
            client = client_;
        }

        const auto started_at = std::chrono::steady_clock::now();
        TtsProviderResponse response;
        if (!client) {
            response.ok = false;
            response.error = "TTS client is not configured";
        } else {
            response = client->synthesize(running.request);
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);

        std::lock_guard<std::mutex> lock(mutex_);
        finish_task_locked(running, std::move(response), elapsed, result);
    }

    return result;
}

std::optional<TtsTask> VoiceTtsService::find_task(TtsTaskId task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<AudioResource> VoiceTtsService::find_audio(const std::string& audio_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto key_it = audio_id_to_cache_key_.find(audio_id);
    if (key_it == audio_id_to_cache_key_.end()) {
        return std::nullopt;
    }

    return find_cached_audio_locked(key_it->second, std::chrono::steady_clock::now());
}

std::vector<TtsTask> VoiceTtsService::list_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TtsTask> tasks;
    tasks.reserve(tasks_.size());
    for (const auto& [_, task] : tasks_) {
        tasks.push_back(task);
    }
    return tasks;
}

std::size_t VoiceTtsService::queued_task_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::size_t>(std::count_if(tasks_.begin(), tasks_.end(), [](const auto& item) {
        return item.second.status == TtsTaskStatus::queued;
    }));
}

std::size_t VoiceTtsService::cache_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return audio_cache_.size();
}

void VoiceTtsService::clear_expired_cache() {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = std::chrono::steady_clock::now();
    for (auto it = audio_cache_.begin(); it != audio_cache_.end();) {
        if (it->second.expires_at <= now) {
            audio_id_to_cache_key_.erase(it->second.id);
            it = audio_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

SubmitResult VoiceTtsService::reject(std::string error) const {
    return SubmitResult{false, std::move(error), false, std::nullopt};
}

std::string VoiceTtsService::build_cache_key(const TtsRequest& request) const {
    std::ostringstream oss;
    oss << request.text << '|'
        << request.voice.voice_id << '|'
        << request.voice.language << '|'
        << request.voice.speed << '|'
        << request.voice.pitch << '|'
        << request.voice.volume << '|'
        << to_string(request.format);

    // 当前内存缓存使用进程内哈希；后续接入 Redis/文件缓存时可替换成稳定哈希。
    return "tts:" + std::to_string(std::hash<std::string>{}(oss.str()));
}

std::string VoiceTtsService::build_audio_id(const std::string& cache_key, TtsTaskId task_id) const {
    return cache_key + ":task:" + std::to_string(task_id);
}

std::optional<AudioResource> VoiceTtsService::find_cached_audio_locked(
    const std::string& cache_key,
    std::chrono::steady_clock::time_point now) const {
    const auto it = audio_cache_.find(cache_key);
    if (it == audio_cache_.end() || it->second.expires_at <= now) {
        return std::nullopt;
    }
    return it->second;
}

void VoiceTtsService::store_cache_locked(const std::string& cache_key, const AudioResource& audio) {
    audio_cache_[cache_key] = audio;
    audio_id_to_cache_key_[audio.id] = cache_key;
    enforce_cache_limit_locked();
}

void VoiceTtsService::enforce_cache_limit_locked() {
    while (audio_cache_.size() > config_.max_cache_items) {
        auto oldest = audio_cache_.begin();
        for (auto it = audio_cache_.begin(); it != audio_cache_.end(); ++it) {
            if (it->second.created_at < oldest->second.created_at) {
                oldest = it;
            }
        }

        audio_id_to_cache_key_.erase(oldest->second.id);
        audio_cache_.erase(oldest);
    }
}

std::optional<VoiceTtsService::RunningTask> VoiceTtsService::take_next_task_locked(
    std::chrono::steady_clock::time_point now) {
    auto selected = tasks_.end();
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
        auto& task = it->second;
        if (task.status != TtsTaskStatus::queued || task.next_attempt_at > now) {
            continue;
        }
        if (selected == tasks_.end() || task.created_at < selected->second.created_at) {
            selected = it;
        }
    }

    if (selected == tasks_.end()) {
        return std::nullopt;
    }

    auto& task = selected->second;
    task.status = TtsTaskStatus::running;
    task.updated_at = now;
    ++task.attempts;

    TtsProviderRequest request;
    request.task_id = task.id;
    request.request = task.request;
    request.attempt = task.attempts;

    return RunningTask{task, request};
}

TtsTask VoiceTtsService::finish_task_locked(const RunningTask& running,
                                            TtsProviderResponse response,
                                            std::chrono::milliseconds elapsed,
                                            TickResult& result) {
    auto& task = tasks_.at(running.task.id);
    const auto now = std::chrono::steady_clock::now();

    const bool timed_out = elapsed > config_.request_timeout;
    if (timed_out) {
        response.ok = false;
        response.error = "TTS request timed out";
    }

    if (response.ok && !response.bytes.empty()) {
        AudioResource audio;
        audio.id = build_audio_id(task.cache_key, task.id);
        audio.format = response.format;
        audio.mime_type = response.mime_type.empty() ? mime_type(response.format) : response.mime_type;
        audio.bytes = std::move(response.bytes);
        audio.duration = response.duration;
        audio.created_at = now;
        audio.expires_at = now + config_.cache_ttl;
        audio.metadata = std::move(response.metadata);

        task.status = TtsTaskStatus::succeeded;
        task.last_error.clear();
        task.audio = audio;
        task.updated_at = now;

        if (task.request.cache_enabled) {
            store_cache_locked(task.cache_key, audio);
        }

        result.succeeded_tasks.push_back(task);
        return task;
    }

    task.last_error = response.error.empty() ? "TTS request failed" : response.error;
    if (task.attempts < max_attempts_from_retries(config_.max_retries)) {
        task.status = TtsTaskStatus::queued;
        task.next_attempt_at = now + std::chrono::milliseconds(config_.retry_backoff.count() * task.attempts);
        task.updated_at = now;
        result.retry_tasks.push_back(task);
        return task;
    }

    task.status = timed_out ? TtsTaskStatus::timed_out : TtsTaskStatus::failed;
    task.updated_at = now;
    result.failed_tasks.push_back(task);
    return task;
}

const char* to_string(AudioFormat format) {
    switch (format) {
        case AudioFormat::wav:
            return "wav";
        case AudioFormat::mp3:
            return "mp3";
        case AudioFormat::pcm16:
            return "pcm16";
        case AudioFormat::ogg:
            return "ogg";
    }
    return "unknown";
}

const char* to_string(TtsTaskStatus status) {
    switch (status) {
        case TtsTaskStatus::queued:
            return "queued";
        case TtsTaskStatus::running:
            return "running";
        case TtsTaskStatus::succeeded:
            return "succeeded";
        case TtsTaskStatus::failed:
            return "failed";
        case TtsTaskStatus::timed_out:
            return "timed_out";
        case TtsTaskStatus::canceled:
            return "canceled";
    }
    return "unknown";
}

const char* mime_type(AudioFormat format) {
    switch (format) {
        case AudioFormat::wav:
            return "audio/wav";
        case AudioFormat::mp3:
            return "audio/mpeg";
        case AudioFormat::pcm16:
            return "audio/L16";
        case AudioFormat::ogg:
            return "audio/ogg";
    }
    return "application/octet-stream";
}

} // namespace rcs::voice_tts
