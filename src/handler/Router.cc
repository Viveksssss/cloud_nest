#include "Router.h"
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>

namespace handler {

// 精确路径
// 将用户指定的精确路径转换为一个“从头到尾严格匹配”的正则表达式，并存储到路由表中
// "/login" -> "^/login$"
void Router::add(std::string const &path, HttpRequest::Method method, Handler handler) {
    std::string escaped = "^" + escapeRegex(path) + "$";
    _routes.emplace_back(escaped, std::vector<std::string>{}, std::move(handler), method);
    spdlog::debug("[Router] 添加路由: {} {}", HttpRequest::methodToString(method), path);
}

// 正则路由
void Router::add(std::string const &pattern, HttpRequest::Method method, Handler handler,
    std::vector<std::string> const &paramNames) {
    std::string fullPattern = pattern;
    if (fullPattern.empty() || fullPattern.front() != '^') {
        fullPattern = "^" + fullPattern;
    }
    if (fullPattern.back() != '$') {
        fullPattern += "$";
    }
    _routes.emplace_back(fullPattern, paramNames, std::move(handler), method);
    spdlog::debug("[Router] 添加路由: {} {} → {} 个参数",
        HttpRequest::methodToString(method),
        pattern,
        paramNames.size());
}

Router::Handler Router::match(std::string const &path, HttpRequest::Method method,
    std::unordered_map<std::string, std::string> &pathParams) const {
    pathParams.clear();

    for (auto const &route: _routes) {
        if (route.method != method) {
            continue;
        }
        // std::match_results<std::string::const_iterator>
        std::smatch matches;
        if (!std::regex_match(path, matches, route.pattern)) {
            continue;
        }

        for (size_t i = 0; i < route.paramNames.size() && i + 1 < matches.size(); ++i) {
            pathParams[route.paramNames[i]] = matches[i + 1].str();
        }

        spdlog::debug("[Router] 匹配成功: {} {} ({} 个参数)",
            HttpRequest::methodToString(method),
            path,
            pathParams.size());

        return route.handler;
    }

    spdlog::debug("[Router] 未匹配: {} {}", HttpRequest::methodToString(method), path);
    return nullptr;
}

std::string Router::escapeRegex(std::string const &str) {
    static std::string const specialChars = R"delim(.*+?^$()[]{}|\)delim";
    std::string result;
    for (char c: str) {
        if (specialChars.find(c) != std::string::npos) {
            result += '\\';
        }
        result += c;
    }
    return result;
}

} // namespace handler
