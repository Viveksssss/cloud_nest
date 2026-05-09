#pragma once

// handler/StaticFileHandler.h
#pragma once
#include <memory>
#include <string>

class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
class HttpRequest;
class HttpResponse;

namespace handler {

class StaticFileHandler {
public:
    /// 构造函数无需参数
    StaticFileHandler() = default;

    /// 返回 index.html / register.html / share.html
    bool serveIndex(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    /// 返回 favicon.ico
    bool serveFavicon(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

private:
    static void sendFile(std::string const &filepath, std::string const &contentType,
        TcpConnectionPtr const &conn, HttpResponse *resp);
    static void send404(TcpConnectionPtr const &conn, HttpResponse *resp);
};

} // namespace handler
