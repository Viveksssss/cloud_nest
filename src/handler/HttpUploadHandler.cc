#include "HttpUploadHandler.h"
#include <HttpContext.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <TcpConnection.h>

namespace handler {
using json = nlohmann::json;

HttpUploadHandler::HttpUploadHandler(
    std::shared_ptr<Database> db, std::string const &uploadDir, int numThreads)
    : _db(std::move(db))
    , _uploadDir(uploadDir) {
    _sessions = std::make_shared<SessionManager>(_db);
    _authHandler = std::make_shared<AuthHandler>(_db, _sessions);
    _fileHandler = std::make_shared<FileHandler>(_db, _sessions, _uploadDir);
    _shareHandler = std::make_shared<ShareHandler>(_db, _sessions, _uploadDir);
    _staticHandler = std::make_shared<StaticFileHandler>();

    // 注册路由
    registerRoutes();
}

void HttpUploadHandler::onConnection(TcpConnectionPtr const &conn) {
    if (conn->connected()) {
        // 为每个连接创建一个 HttpContext，用于存储上传/下载上下文
        conn->setContext(std::make_shared<HttpContext>());
        spdlog::debug("新连接: {}", conn->peerAddress().toIpPort());
    } else {
        spdlog::debug("连接关闭: {}", conn->peerAddress().toIpPort());
        // 清理上下文（上传/下载上下文中的文件会自动关闭）
        conn->setContext(nullptr);
    }
}

bool HttpUploadHandler::onRequest(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    try {
        std::unordered_map<std::string, std::string> params;
        auto handler = _router.match(req.path(), req.method(), params);

        if (!handler) {
            // 返回 404
            json body = {{"code", 404}, {"message", "Not Found"}};
            resp->setStatusCode(HttpResponse::NotFound);
            resp->setStatusMessage("Not Found");
            resp->setContentType("application/json; charset=utf-8");
            resp->addHeader("Connection", "close");
            resp->setBody(body.dump());
            conn->setWriteCompleteCallback(
                [self = shared_from_this()](TcpConnectionPtr const &c) { c->shutdown(); });
            return true;
        }
        req.setPathParams(params);

        return handler(conn, req, resp);
    } catch (std::exception const &e) {
        spdlog::error("请求处理异常: {}", e.what());
        json body = {{"code", 500}, {"message", "Internal Server Error"}};
        resp->setStatusCode(HttpResponse::InternalServerError);
        resp->setStatusMessage("Internal Server Error");
        resp->setContentType("application/json; charset=utf-8");
        resp->addHeader("Connection", "close");
        resp->setBody(body.dump());
        conn->setWriteCompleteCallback(
            [self = shared_from_this()](TcpConnectionPtr const &c) { c->shutdown(); });
        return true;
    }
}

void HttpUploadHandler::registerRoutes() {
    using Method = HttpRequest::Method;
    // ---------- 静态页面 ----------
    _router.add("/", Method::Get, [this](auto &conn, auto &req, auto *resp) {
        return _staticHandler->serveIndex(conn, req, resp);
    });
    _router.add("/index.html", Method::Get, [this](auto &conn, auto &req, auto *resp) {
        return _staticHandler->serveIndex(conn, req, resp);
    });
    _router.add("/register.html", Method::Get, [this](auto &conn, auto &req, auto *resp) {
        return _staticHandler->serveIndex(conn, req, resp);
    });
    _router.add("/favicon.ico", Method::Get, [this](auto &conn, auto &req, auto *resp) {
        return _staticHandler->serveFavicon(conn, req, resp);
    });

    // ---------- 认证 ----------
    _router.add("/register", Method::Post, [this](auto &conn, auto &req, auto *resp) {
        return _authHandler->handleRegister(conn, req, resp);
    });
    _router.add("/login", Method::Post, [this](auto &conn, auto &req, auto *resp) {
        return _authHandler->handleLogin(conn, req, resp);
    });
    _router.add("/logout", Method::Post, [this](auto &conn, auto &req, auto *resp) {
        return _authHandler->handleLogout(conn, req, resp);
    });

    // ---------- 文件 ----------
    _router.add("/upload", Method::Post, [this](auto &conn, auto &req, auto *resp) {
        return _fileHandler->handleUpload(conn, req, resp);
    });
    _router.add("/files", Method::Get, [this](auto &conn, auto &req, auto *resp) {
        return _fileHandler->handleListFiles(conn, req, resp);
    });
    // 下载（GET 和 HEAD）
    _router.add("/download/([^/]+)",
        Method::Get,
        [this](auto &conn, auto &req, auto *resp) {
            return _fileHandler->handleDownload(conn, req, resp);
        },
        {"filename"});
    _router.add("/download/([^/]+)",
        Method::Head,
        [this](auto &conn, auto &req, auto *resp) {
            return _fileHandler->handleDownload(conn, req, resp);
        },
        {"filename"});
    // 删除
    _router.add("/delete/([^/]+)",
        Method::Delete,
        [this](auto &conn, auto &req, auto *resp) {
            return _fileHandler->handleDelete(conn, req, resp);
        },
        {"filename"});

    // ---------- 分享 ----------
    _router.add("/share", Method::Post, [this](auto &conn, auto &req, auto *resp) {
        return _shareHandler->handleShareFile(conn, req, resp);
    });
    _router.add("/share/([^/]+)",
        Method::Get,
        [this](auto &conn, auto &req, auto *resp) {
            return _shareHandler->handleShareAccess(conn, req, resp);
        },
        {"code"});
    _router.add("/share/download/([^/]+)",
        Method::Get,
        [this](auto &conn, auto &req, auto *resp) {
            return _shareHandler->handleShareDownload(conn, req, resp);
        },
        {"filename"});
    _router.add("/share/info/([^/]+)",
        Method::Get,
        [this](auto &conn, auto &req, auto *resp) {
            return _shareHandler->handleShareInfo(conn, req, resp);
        },
        {"code"});
    _router.add("/users/search", Method::Get, [this](auto &conn, auto &req, auto *resp) {
        return _shareHandler->handleSearchUsers(conn, req, resp);
    });
}
} // namespace handler
