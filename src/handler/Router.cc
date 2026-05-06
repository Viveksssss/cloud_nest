#include "Router.h"
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <spdlog/spdlog.h>

namespace handler {

void Router::add(std::string const &path, HttpRequest::Method method, Handler handler) {
    std::string escaped = "^" + escapeRegex(path) + "$";
    _routes.emplace_back(escaped, std::vector<std::string>{}, std::move(handler), method);
    spdlog::debug("[Router] 添加路由: {} {}", HttpRequest::methodToString(method), path);
}

} // namespace handler
