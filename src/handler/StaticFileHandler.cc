#include "StaticFileHandler.h"
#include "utils/FileUtils.h"
#include <fstream>
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <linux/limits.h>
#include <spdlog/spdlog.h>
#include <TcpConnection.h>
#include <unistd.h>

namespace handler {
bool StaticFileHandler::serveIndex(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string path = req.path();
    std::string projectRoot = utils::getExecuteRoot();
    std::string filePath;
    if (path == "/" || path == "/index.html") {
        filePath = projectRoot + "/static/index.html";
        spdlog::info("filePath: {}", filePath);
    } else if (path == "/register.html") {
        filePath = projectRoot + "/static/register.html";
    } else if (path == "/share.html" || path.find("/share/") == 0) {
        filePath = projectRoot + "/static/share.html";
    } else {
        send404(conn, resp);
        return true;
    }
    sendFile(filePath, "text/html; charset=utf-8", conn, resp);
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
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

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
} // namespace handler
