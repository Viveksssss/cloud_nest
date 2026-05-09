#include "AuthHandler.h"
#include "db/Database.h"
#include "handler/SessionManager.h"
#include "utils/CodeGenerator.h"
#include "utils/StringUtils.h"
#include <exception>
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <TcpConnection.h>

using json = nlohmann::json;

namespace handler {

AuthHandler::AuthHandler(std::shared_ptr<Database> db, std::shared_ptr<SessionManager> sessions)
    : _db(std::move(db))
    , _sessions(std::move(sessions)) {
}

bool AuthHandler::handleRegister(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    try {
        json requestData = json::parse(req.body());
        std::string username = requestData.value("username", "");
        std::string password = requestData.value("password", "");
        std::string email = requestData.value("email", "");
        std::string error = validateRegisterInput(username, password, email);
        if (!error.empty()) {
            utils::sendError(resp, error, 400, conn);
            return true;
        }
        auto existing = _db->queryOne("SELECT id FROM users WHERE username=$1", {username});
        if (!existing.empty()) {
            utils::sendError(resp, "用户名已存在", 400, conn);
            return true;
        }

        std::string hashedPassword = hashPassword(password);
        std::string sql
            = "INSERT INTO users (username,password,email) VALUES ($1,$2,$3) RETURNING id";
        auto res = _db->queryParams(sql, {username, hashedPassword, email});
        if (res.empty()) {
            utils::sendError(resp, "注册失败,请稍后重试", 500, conn);
            return true;
        }

        int userId = std::stoi(res[0]["id"]);
        json response = {{"code", 0}, {"message", "注册成功"}, {"userId", userId}};
        utils::sendJson(resp, response.dump(), 200, conn);
        return true;
    } catch (json::parse_error const &e) {
        utils::sendError(resp, "请求格式错误", 400, conn);
        return true;
    } catch (std::exception const &e) {
        spdlog::error("[Auth] 注册异常: {}", e.what());
        utils::sendError(resp, "服务器内部错误", 500, conn);
        return true;
    }
}

bool AuthHandler::handleLogin(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    try {
        json requestData = json::parse(req.body());
        std::string username = requestData.value("username", "");
        std::string password = requestData.value("password", "");
        if (username.empty() || password.empty()) {
            utils::sendError(resp, "用户名和密码不能为空", 400, conn);
            return true;
        }

        auto row = _db->queryOne(
            "SELECT id,username,password FROM users WHERE username = $1", {username});

        if (row.empty()) {
            utils::sendError(resp, "用户名或密码错误", 401, conn);
            return true;
        }

        int userId = std::stoi(row["id"]);
        std::string storedHash = row["password"];
        std::string inputHash = hashPassword(password);
        if (inputHash != storedHash) {
            utils::sendError(resp, "用户名或密码错误", 401, conn);
            return true;
        }

        std::string sessionId = _sessions->createSession(userId, username);
        if (sessionId.empty()) {
            utils::sendError(resp, "登录失败,无法创建会话", 500, conn);
            return true;
        }

        json response = {
            {"code", 0},
            {"message", "登录成功"},
            {"sessionId", sessionId},
            {"userId", userId},
            {"username", username},
        };
        utils::sendJson(resp, response.dump(), 200, conn);
        return true;
    } catch (json::parse_error const &e) {
        utils::sendError(resp, "请求格式错误", 400, conn);
        return true;
    } catch (std::exception const &e) {
        spdlog::error("[Auth] 登录异常: {}", e.what());
        utils::sendError(resp, "服务器内部错误", 500, conn);
        return true;
    }
}

bool AuthHandler::handleLogout(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string sessionId = req.getHeader("X-Session-ID");
    if (!sessionId.empty()) {
        _sessions->destroy(sessionId);
    }
    json response = {{"code", 0}, {"message", "登出成功"}};
    utils::sendJson(resp, response.dump(), 200, conn);
    return true;
}

std::string AuthHandler::hashPassword(std::string const &password) {
    return utils::sha256(password);
}

std::string AuthHandler::validateRegisterInput(
    std::string const &username, std::string const &password, std::string const &email) {
    if (username.empty()) {
        return "用户名不可以为空";
    }
    if (username.length() < 3 || username.length() > 20) {
        return "用户名长度在3-20个字符之间";
    }
    if (password.empty()) {
        return "密码不能为空";
    }
    if (password.length() < 8) {
        return "密码长度至少8位";
    }
    if (!email.empty()) {
        auto at = email.find("@");
        if (at == std::string::npos || at == 0 || at == email.length() - 1) {
            return "邮箱格式不正确";
        }
    }
    return ""; // 通过
}

} // namespace handler
