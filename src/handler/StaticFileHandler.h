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
    StaticFileHandler();

    /// 返回 index.html / register.html / share.html
    bool serveIndex(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    /// 返回 favicon.ico
    bool serveFavicon(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

private:
    static void sendFile(std::string const &filepath, std::string const &contentType,
        TcpConnectionPtr const &conn, HttpResponse *resp);
    static void send404(TcpConnectionPtr const &conn, HttpResponse *resp);

private:
    std::string m_cachedIndexHtml;
    std::string m_cachedRegisterHtml;
    std::string m_cachedShareHtml;
    std::string m_cachedFavicon;

    std::string m_cachedIndexHtmlGz;
    std::string m_cachedRegisterHtmlGz;
    std::string m_cachedShareHtmlGz;

    static std::string compressGzip(std::string const &input);
    void compressStaticFiles(); // 新增

    void cacheStaticFiles();
    static std::string readFileContent(std::string const &path);
};

} // namespace handler
