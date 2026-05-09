#pragma once

#include <memory>
#include <string>

class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
class HttpRequest;
class HttpResponse;
class Database;

namespace handler {
class SessionManager;

class ShareHandler : public std::enable_shared_from_this<ShareHandler> {
public:
    ShareHandler(std::shared_ptr<Database> db, std::shared_ptr<SessionManager> sessions,
        std::string const &uploadDir);

    // POST /share           创建分享
    bool handleShareFile(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    // GET  /share/{code}     分享页面 / JSON 数据
    bool handleShareAccess(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    // GET  /share/download/{filename}?code=...  通过分享码下载
    bool handleShareDownload(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    // GET  /share/info/{code} 查询分享信息
    bool handleShareInfo(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    // GET  /users/search?keyword=...  搜索用户（用于指定用户分享）
    bool handleSearchUsers(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

private:
    std::shared_ptr<Database> _db;
    std::shared_ptr<SessionManager> _sessions;
    std::string _uploadDir;
};

} // namespace handler
