#pragma once
#include "AuthHandler.h"
#include "FileHandler.h"
#include "Router.h"
#include "SessionManager.h"
#include "ShareHandler.h"
#include "StaticFileHandler.h"
#include <memory>
#include <string>

class Database;
class EventLoop; // 视需要而定
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
class HttpRequest;
class HttpResponse;

namespace handler {

class HttpUploadHandler : public std::enable_shared_from_this<HttpUploadHandler> {
public:
    /**
     * @brief 构造函数，初始化所有处理模块和路由
     * @param db 数据库接口
     * @param uploadDir 文件上传目录
     * @param numThreads 线程池大小（用于可能的后台任务，暂未使用）
     */
    HttpUploadHandler() = default;

    HttpUploadHandler(std::shared_ptr<Database> db,
        std::string const &uploadDir = "/home/vivek/tmp/uploads", int numThreads = 8);
    ~HttpUploadHandler() = default;

    // 连接回调
    void onConnection(TcpConnectionPtr const &conn);

    // HTTP 请求回调（由 HttpServer 调用）
    bool onRequest(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

private:
    /// 注册所有路由到 Router
    void registerRoutes();

    // 基础设施
    std::shared_ptr<Database> _db;
    Router _router;

    // 子模块
    std::shared_ptr<SessionManager> _sessions;
    std::shared_ptr<AuthHandler> _authHandler;
    std::shared_ptr<FileHandler> _fileHandler;
    std::shared_ptr<ShareHandler> _shareHandler;
    std::shared_ptr<StaticFileHandler> _staticHandler;

    std::string _uploadDir;
};

} // namespace handler
