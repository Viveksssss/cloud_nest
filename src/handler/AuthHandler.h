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

/**
 * @brief 用户认证处理器
 *
 * 处理：
 * - POST /register  用户注册
 * - POST /login     用户登录
 * - POST /logout    用户登出
 */
class AuthHandler {
public:
    /**
     * @param db 数据库接口
     * @param sessions 会话管理器
     */
    AuthHandler(std::shared_ptr<Database> db, std::shared_ptr<SessionManager> sessions);

    /// 处理用户注册
    bool handleRegister(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    /// 处理用户登录
    bool handleLogin(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    /// 处理用户登出
    bool handleLogout(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

private:
    std::shared_ptr<Database> _db;
    std::shared_ptr<SessionManager> _sessions;

    /// 密码哈希（委托给 utils）
    static std::string hashPassword(std::string const &password);

    /// 验证注册输入
    static std::string validateRegisterInput(
        std::string const &username, std::string const &password, std::string const &email);
};

} // namespace handler
