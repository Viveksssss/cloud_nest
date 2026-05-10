#include "StaticFileHandler.h"
#include "utils/FileUtils.h"
#include <fstream>
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <linux/limits.h>
#include <spdlog/spdlog.h>
#include <TcpConnection.h>
#include <unistd.h>
#include <zlib.h>

namespace handler {
StaticFileHandler::StaticFileHandler() {
    cacheStaticFiles();
}

bool StaticFileHandler::serveIndex(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string path = req.path();
    std::string html;
    std::string gzHtml;
    if (path == "/" || path == "/index.html") {
        html = m_cachedIndexHtml;
        gzHtml = m_cachedIndexHtmlGz;
    } else if (path == "/register.html") {
        html = m_cachedRegisterHtml;
        gzHtml = m_cachedRegisterHtmlGz;
    } else if (path == "/share.html" || path.find("/share/") == 0) {
        html = m_cachedShareHtml;
        gzHtml = m_cachedShareHtmlGz;
    } else {
        send404(conn, resp);
        return true;
    }
    if (html.empty()) {
        send404(conn, resp);
        return true;
    }

    // 判断客户端是否支持 gzip
    std::string acceptEncoding = req.getHeader("Accept-Encoding");
    bool supportGzip = (acceptEncoding.find("gzip") != std::string::npos);

    if (supportGzip && !gzHtml.empty()) {
        resp->setBody(gzHtml);
        resp->setContentType("text/html; charset=utf-8");
        resp->addHeader("Content-Encoding", "gzip");
        resp->addHeader("Vary", "Accept-Encoding");
    } else {
        resp->setBody(html);
        resp->setContentType("text/html; charset=utf-8");
    }

    resp->setStatusCode(HttpResponse::HttpStatusCode::OK);
    resp->setStatusMessage("OK");
    resp->addHeader("Connection", "Keep-Alive");
    return true;
}

bool StaticFileHandler::serveFavicon(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string faviconPath = utils::getExecuteRoot() + "/static/favicon.ico";
    sendFile(faviconPath, "image/x-icon", conn, resp);
    return true;
}

void StaticFileHandler::sendFile(std::string const &filepath, std::string const &contentType,
    TcpConnectionPtr const &conn, HttpResponse *resp) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        send404(conn, resp);
        return;
    }
    static std::string content(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    file.close();
    resp->setStatusCode(HttpResponse::OK);
    resp->setStatusMessage("OK");
    resp->setContentType(contentType);
    resp->addHeader("Connection", "close");
    resp->setBody(content);
    conn->setWriteCompleteCallback([](TcpConnectionPtr const &c) { c->shutdown(); });
}

void StaticFileHandler::send404(TcpConnectionPtr const &conn, HttpResponse *resp) {
    resp->setStatusCode(HttpResponse::NotFound);
    resp->setStatusMessage("Not Found");
    resp->setContentType("text/plain");
    resp->addHeader("Connection", "close");
    resp->setBody("404 Not Found");
    conn->setWriteCompleteCallback([](TcpConnectionPtr const &c) { c->shutdown(); });
}

std::string StaticFileHandler::compressGzip(std::string const &input) {
    if (input.empty()) {
        return "";
    }

    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;

    // 初始化 deflate，使用 gzip 格式 (windowBits = MAX_WBITS + 16)
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY)
        != Z_OK) {
        return "";
    }

    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    zs.avail_in = static_cast<unsigned int>(input.size());

    std::string output;
    char buffer[32768];
    int ret;

    do {
        zs.next_out = reinterpret_cast<Bytef *>(buffer);
        zs.avail_out = sizeof(buffer);
        ret = deflate(&zs, Z_FINISH);
        if (output.size() < zs.total_out) {
            output.append(buffer, zs.total_out - output.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        return ""; // 压缩失败
    }
    return output;
}

void StaticFileHandler::compressStaticFiles() {
    m_cachedIndexHtmlGz = compressGzip(m_cachedIndexHtml);
    m_cachedRegisterHtmlGz = compressGzip(m_cachedRegisterHtml);
    m_cachedShareHtmlGz = compressGzip(m_cachedShareHtml);
}

void StaticFileHandler::cacheStaticFiles() {
    std::string root = utils::getExecuteRoot();
    m_cachedIndexHtml = readFileContent(root + "/static/index.html");
    m_cachedRegisterHtml = readFileContent(root + "/static/register.html");
    m_cachedShareHtml = readFileContent(root + "/static/share.html");

    // favicon 是二进制，单独处理
    std::string faviconPath = root + "/static/favicon.ico";
    std::ifstream file(faviconPath, std::ios::binary);
    if (file) {
        m_cachedFavicon.assign(
            (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    compressStaticFiles();
}

std::string StaticFileHandler::readFileContent(std::string const &path) {
    std::ifstream file(path);
    if (!file) {
        spdlog::error("缓存静态文件失败: {}", path);
        return "";
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace handler
