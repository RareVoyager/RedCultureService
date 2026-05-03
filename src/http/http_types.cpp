#include "rcs/http/http_types.hpp"

namespace rcs::http {

HttpResponse HttpResponse::json(int status_code, std::string body)
{
    HttpResponse response;
    response.status_code = status_code;
    response.content_type = "application/json; charset=utf-8";
    response.body = std::move(body);
    return response;
}

HttpResponse HttpResponse::text(int status_code, std::string body, std::string content_type)
{
    HttpResponse response;
    response.status_code = status_code;
    response.content_type = std::move(content_type);
    response.body = std::move(body);
    return response;
}

HttpResponse HttpResponse::noContent()
{
    HttpResponse response;
    response.status_code = 204;
    response.content_type = "text/plain; charset=utf-8";
    return response;
}

HttpResponse HttpResponse::notFound()
{
    return text(404, "not found");
}

HttpResponse HttpResponse::badRequest(std::string message)
{
    return text(400, std::move(message));
}

HttpResponse HttpResponse::internalError(std::string message)
{
    return text(500, std::move(message));
}

} // namespace rcs::http
