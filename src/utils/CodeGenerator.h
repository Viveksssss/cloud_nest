// utils/CodeGenerator.h
#pragma once
#include <Callbacks.h>
#include <HttpResponse.h>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <TcpConnection.h>
#include <thread>

namespace utils {

using json = nlohmann::json;

namespace {

static inline std::mt19937 &getRng() {
    static thread_local std::mt19937 rng(
        std::random_device{}() ^ std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return rng;
}

} // namespace

/**
 * @brief 生成指定长度的随机字符串
 *
 * @param length 长度
 * @param charset 字符集
 * @return 随机字符串
 */
static inline std::string generateRandomString(size_t length, std::string const &charset) {
    if (charset.empty() || length == 0) {
        return "";
    }
    std::uniform_int_distribution<size_t> dis(0, charset.size() - 1);
    auto &rng = getRng();
    std::string result;
    for (size_t i = 0; i < length; ++i) {
        result.push_back(charset[dis(rng)]);
    }
    return result;
}

/**
 * @brief 生成随机会话 ID
 *
 * 格式: 32 位小写字母+数字组合
 * 字符集: a-z, 0-9
 * 示例: "k3m7xq2a9b1y8n4f0d5w6r3t8p2l"
 *
 * @return 32 位随机字符串
 */
static inline std::string generateSessionId() {
    static std::string const charset = "abcdefghijklmnopqrstuvwxyz0123456789";
    return generateRandomString(32, charset);
}

/**
 * @brief 生成分享码（用于 URL）
 *
 * 格式: 32 位小写字母+数字组合
 * 字符集: a-z, 0-9
 * 用途: 构建 /share/{shareCode} 链接
 *
 * @return 32 位随机字符串
 */
static inline std::string generateShareCode() {
    static std::string const charset = "abcdefghijklmnopqrstuvwxyz0123456789";
    return generateRandomString(32, charset);
}

/**
 * @brief 生成提取码
 *
 * 格式: 6 位大写字母+数字组合
 * 字符集: 0-9, A-Z（排除容易混淆的字符: 0/O, 1/I, 2/Z 等）
 * 示例: "A3X9K2"
 *
 * @return 6 位随机字符串
 */
static inline std::string generateExtractCode() {
    static std::string const charset = "34679ACDEFGHJKLMNPQRTUVWXY";
    return generateRandomString(6, charset);
}

static inline void sendJson(
    HttpResponse *resp, std::string const &body, int code, TcpConnectionPtr const &conn) {
    resp->setStatusCode(static_cast<HttpResponse::HttpStatusCode>(code));
    resp->setStatusMessage(code == 200 ? "OK" : "Error");
    resp->setContentType("application/json; charset=utf-8");
    resp->addHeader("Connection", "close");
    resp->setBody(body);
    if (conn) {
        conn->setWriteCompleteCallback([](TcpConnectionPtr const &c) { c->shutdown(); });
    }
}

static inline void sendError(
    HttpResponse *resp, std::string const &msg, int code, TcpConnectionPtr const &conn) {
    json body = {{"code", code}, {"message", msg}};
    sendJson(resp, body.dump(), code, conn);
}

} // namespace utils
