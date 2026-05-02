#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

// TTS 音频资源接口控制器：根据 audio_id 返回可播放的音频字节。
class TtsController : public std::enable_shared_from_this<TtsController> {
public:
    explicit TtsController(std::shared_ptr<application::ServiceContext> context);

    void register_routes(http::HttpRouter& router);

private:
    http::HttpResponse get_audio(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers
