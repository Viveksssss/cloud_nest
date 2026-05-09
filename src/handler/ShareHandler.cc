
#include "ShareHandler.h"
#include "db/Database.h"
#include "handler/FileDownContext.h"
#include "handler/SessionManager.h"
#include "utils/CodeGenerator.h"
#include "utils/FileUtils.h"
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <HttpContext.h>
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>

namespace handler {

using json = nlohmann::json;
namespace fs = std::filesystem;

ShareHandler::ShareHandler(std::shared_ptr<Database> db, std::shared_ptr<SessionManager> sessions,
    std::string const &uploadDir)
    : _db(std::move(db))
    , _sessions(std::move(sessions))
    , _uploadDir(uploadDir) {
}

bool ShareHandler::handleShareFile(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    // 验证登录
    std::string sessionId = req.getHeader("X-Session-ID");
    int ownerId = 0;
    std::string ownerName;
    if (!_sessions->validate(sessionId, ownerId, ownerName)) {
        utils::sendError(resp, "未登录或会话已过期", 401, conn);
        return true;
    }

    try {
        json data = json::parse(req.body());
        int fileId = data.at("fileId").get<int>();
        std::string shareType = data.at("shareType").get<std::string>();

        // 检查文件所有权
        auto fileRow = _db->queryOne("SELECT 1 FROM files WHERE id = $1 AND user_id = $2",
            {std::to_string(fileId), std::to_string(ownerId)});
        if (fileRow.empty()) {
            utils::sendError(resp, "您没有权限分享此文件", 403, conn);
            return true;
        }

        // 取消分享（设为私有）
        if (shareType == "private") {
            _db->executeParams(
                "DELETE FROM file_shares WHERE file_id = $1", {std::to_string(fileId)});
            json result = {{"code", 0}, {"message", "文件已设为私有"}};
            utils::sendJson(resp, result.dump(), 200, conn);
            return true;
        }

        // 生成分享码
        std::string shareCode = utils::generateShareCode();

        // 处理不同类型的分享，准备额外参数
        std::string sharedWithId; // 仅 user 类型使用
        std::string extractCode;  // 仅 protected 类型使用（原始提取码，无引号）
        int expireHours = 0;      // 过期小时数，0 表示不过期

        if (shareType == "user") {
            if (!data.contains("sharedWithId")) {
                utils::sendError(resp, "缺少目标用户ID", 400, conn);
                return true;
            }
            int targetId = data["sharedWithId"].get<int>();
            // 检查是否已经分享给该用户
            auto existing = _db->queryOne("SELECT 1 FROM file_shares WHERE file_id = $1 AND "
                                          "shared_with_id = $2 AND share_type = 'user'",
                {std::to_string(fileId), std::to_string(targetId)});
            if (!existing.empty()) {
                utils::sendError(resp, "已经分享给该用户", 400, conn);
                return true;
            }
            sharedWithId = std::to_string(targetId);
        } else if (shareType == "protected") {
            extractCode = utils::generateExtractCode(); // 原始提取码，不加引号
        }

        // 过期时间处理
        if (data.contains("expireTime") && !data["expireTime"].is_null()) {
            int hours = data["expireTime"].get<int>();
            if (hours > 0) {
                expireHours = hours;
            }
        }

        // --- 动态构建 SQL 和参数 ---
        std::string columns = "file_id, owner_id, share_type, share_code";
        std::string values = "$1, $2, $3, $4";
        std::vector<std::string> params
            = {std::to_string(fileId), std::to_string(ownerId), shareType, shareCode};
        int paramIndex = 5; // 下一个参数编号

        // 1. shared_with_id（仅 user 类型需要）
        if (shareType == "user") {
            columns += ", shared_with_id";
            values += ", $" + std::to_string(paramIndex++);
            params.push_back(sharedWithId);
        }

        // 2. extract_code（所有类型都有此列）
        columns += ", extract_code";
        if (shareType == "protected") {
            values += ", $" + std::to_string(paramIndex++);
            params.push_back(extractCode); // 原始提取码
        } else {
            values += ", NULL";
        }

        // 3. expire_time（所有类型都有此列）
        columns += ", expire_time";
        if (expireHours > 0) {
            // 使用参数化的时间计算，避免 SQL 注入
            values += ", NOW() + INTERVAL '1 hour' * $" + std::to_string(paramIndex++);
            params.push_back(std::to_string(expireHours));
        } else {
            values += ", NULL";
        }

        std::string sql
            = "INSERT INTO file_shares (" + columns + ") VALUES (" + values + ") RETURNING id";

        // 执行插入
        auto res = _db->queryParams(sql, params);
        if (res.empty()) {
            utils::sendError(resp, "创建分享失败", 500, conn);
            return true;
        }

        int shareId = std::stoi(res[0]["id"]);

        // 构造响应 JSON
        json respJson = {{"code", 0},
            {"message", "分享成功"},
            {"shareId", shareId},
            {"shareType", shareType},
            {"shareCode", shareCode},
            {"shareLink", "/share/" + shareCode}};
        if (shareType == "protected") {
            respJson["extractCode"] = extractCode; // 返回原始提取码
        }
        if (shareType == "user") {
            respJson["sharedWithId"] = std::stoi(sharedWithId);
        }

        utils::sendJson(resp, respJson.dump(), 200, conn);
        return true;

    } catch (json::parse_error const &e) {
        spdlog::error("[Share] JSON 解析错误: {}", e.what());
        utils::sendError(resp, "请求格式错误", 400, conn);
        return true;
    } catch (std::exception const &e) {
        spdlog::error("[Share] 创建分享异常: {}", e.what());
        utils::sendError(resp, "服务器内部错误", 500, conn);
        return true;
    }
}

bool ShareHandler::handleShareAccess(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string shareCode = req.getPathParam("code");
    if (shareCode.empty() || shareCode.length() != 32) {
        utils::sendError(resp, "无效的分享连接", 400, conn);
        return true;
    }

    std::string accept = req.getHeader("Accept");
    bool wantJson = (accept.find("application/json") != std::string::npos
                     || req.getHeader("X-Requested-With") == "XMLHttpRequest");

    std::string providedCode = req.getQuery("code", "");

    auto rows
        = _db->queryParams("SELECT fs.*, f.filename, f.original_filename, f.file_size, f.file_type,"
                           "       u.username as owner_username, f.user_id "
                           "FROM file_shares fs "
                           "JOIN files f ON fs.file_id = f.id "
                           "JOIN users u ON f.user_id = u.id "
                           "WHERE fs.share_code = $1 "
                           "AND (fs.expire_time IS NULL OR fs.expire_time > NOW())",
            {shareCode});

    if (rows.empty()) {
        utils::sendError(resp, "分享链接已失效或不存在", 404, conn);
        return true;
    }

    auto &row = rows[0];
    std::string shareType = row["share_type"];
    std::string dbExtractCode = row["extract_code"];
    std::string serverFilename = row["filename"];
    std::string originalFilename = row["original_filename"];

    std::string sessionId = req.getHeader("X-Session-ID");
    int currentUserId = 0;
    std::string currentUser;
    bool loggedIn = _sessions->validate(sessionId, currentUserId, currentUser);
    bool isOwner = (loggedIn && std::stoi(row["user_id"]) == currentUserId);

    bool canAccess = false;
    if (isOwner) {
        canAccess = true; // 文件所有者直接允许
    } else if (shareType == "public") {
        canAccess = true;
    } else if (shareType == "protected") {
        if (!providedCode.empty() && providedCode == dbExtractCode) {
            canAccess = true;
        }
    } else if (shareType == "user") {
        int sharedWithId = row["shared_with_id"].empty() ? 0 : std::stoi(row["shared_with_id"]);
        if (loggedIn && currentUserId == sharedWithId) {
            canAccess = true;
        }
    }

    // 如果权限不足
    if (!canAccess) {
        // 对于普通的页面请求（非 AJAX），始终返回 share.html，
        // 让前端页面自行通过 /share/info 获取详情并提示用户
        if (!wantJson) {
            std::string projectRoot = utils::getExecuteRoot();
            std::string htmlPath = projectRoot + "/static/share.html";
            std::ifstream file(htmlPath);
            if (!file.is_open()) {
                utils::sendError(resp, "页面未找到", 500, conn);
                return true;
            }
            std::string html(
                (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            resp->setStatusCode(HttpResponse::OK);
            resp->setContentType("text/html; charset=utf-8");
            resp->addHeader("X-Share-Code", shareCode); // 传递分享码
            resp->addHeader("X-Share-Type", shareType); // 传递分享类型
            resp->addHeader("Connection", "close");
            resp->setBody(html);
            conn->setWriteCompleteCallback([](TcpConnectionPtr const &c) { c->shutdown(); });
            return true;
        } else {
            // AJAX 请求才返回对应的错误信息
            if (shareType == "protected") {
                utils::sendError(resp, "需要正确的提取码", 403, conn);
            } else if (shareType == "user") {
                utils::sendError(resp, "您没有权限访问此文件，请先登录", 403, conn);
            } else {
                utils::sendError(resp, "您没有权限访问此文件", 403, conn);
            }
            return true;
        }
    }

    if (wantJson) {
        json fileInfo = {
            {"id", std::stoi(row["id"])},
            {"fileId", std::stoi(row["file_id"])},
            {"ownerId", std::stoi(row["owner_id"])},
            {"sharedWithId", row["shared_with_id"].empty() ? 0 : std::stoi(row["shared_with_id"])},
            {"shareType", shareType},
            {"shareCode", shareCode},
            {"createdAt", row["created_at"]},
            {"expireTime", row["expire_time"]},
            {"filename", serverFilename},
            {"originalName", originalFilename},
            {"size", std::stoull(row["file_size"])},
            {"type", row["file_type"]},
            {"ownerUsername", row["owner_username"]},
            {"isOwner", isOwner},
        };

        json respJson = {
            {"code", 0},
            {"message", "success"},
            {"file", fileInfo},
            {"downloadUrl", "/share/download/" + serverFilename + "?code=" + shareCode},
        };

        utils::sendJson(resp, respJson.dump(), 200, conn);
        return true;
    } else {
        // 返回 share.html 页面
        // 这里调用静态文件服务，假设外部注入一个 staticHandler 或直接处理
        // 简单做法：作为 index.html 类似处理，但返回 share.html；这里需要将 shareCode 传递给前端
        // 由于框架可能没有直接返回页面的函数，我们返回一个简单的重定向或由专门的 StaticFileHandler
        // 处理 在集成时，可在主类中判断是分享访问并调用 StaticFileHandler::serveSharePage
        // 这里为了完整性，给出示例：直接返回 share.html 内容

        std::string projectRoot = utils::getExecuteRoot();
        std::string htmlPath = projectRoot + "/static/share.html";
        std::ifstream file(htmlPath);
        if (!file.is_open()) {
            utils::sendJson(resp, "页面未找到", 500, conn);
            return true;
        }

        std::string html((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        resp->setStatusCode(HttpResponse::OK);
        resp->setContentType("text/html; charset=utf-8");
        resp->addHeader("X-Share-code", shareCode);
        resp->addHeader("Connection", "close");
        resp->setBody(html);
        conn->setWriteCompleteCallback(
            [self = shared_from_this()](TcpConnectionPtr const &conn) { conn->shutdown(); });
        return true;
    }
}

bool ShareHandler::handleShareDownload(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string filename = req.getPathParam("filename");
    std::string shareCode = req.getQuery("code", "");
    std::string extractCode = req.getQuery("extract_code", "");

    if (filename.empty() || shareCode.empty()) {
        utils::sendError(resp, "参数错误", 400, conn);
        return true;
    }

    auto rows = _db->queryParams(
        "SELECT f.id, f.filename, f.original_filename, fs.share_type, fs.extract_code, "
        "       fs.shared_with_id, f.user_id "
        "FROM files f JOIN file_shares fs ON f.id = fs.file_id "
        "WHERE f.filename = $1 AND fs.share_code = $2 "
        "AND (fs.expire_time IS NULL OR fs.expire_time > NOW())",
        {filename, shareCode});

    if (rows.empty()) {
        utils::sendError(resp, "分享不存在或已过期", 404, conn);
        return true;
    }

    auto &info = rows[0];
    std::string shareType = info["share_type"];
    std::string dbExtract = info["extract_code"];
    int fileOwnerId = std::stoi(info["user_id"]);
    std::string serverFilename = info["filename"];
    std::string originalFilename = info["original_filename"];

    // 权限
    bool allowed = false;
    if (shareType == "public") {
        allowed = true;
    } else if (shareType == "protected") {
        if (!extractCode.empty() && extractCode == dbExtract) {
            allowed = true;
        }
    } else if (shareType == "user") {
        std::string sessionId = req.getHeader("X-Session-ID");
        int userId = 0;
        std::string userName;
        if (_sessions->validate(sessionId, userId, userName)) {
            int targetId = info["shared_with_id"].empty() ? 0 : std::stoi(info["shared_with_id"]);
            if (userId == targetId || userId == fileOwnerId) {
                allowed = true;
            }
        }
    }

    if (!allowed) {
        utils::sendError(resp, "您没有权限下载此文件", 403, conn);
        return true;
    }

    std::string filepath = _uploadDir + "/" + serverFilename;
    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        utils::sendError(resp, "文件未找到", 404, conn);
        return true;
    }

    size_t fileSize = fs::file_size(filepath);
    if (req.method() == HttpRequest::Method::Head) {
        resp->setStatusCode(HttpResponse::OK);
        resp->setContentType("application/octet-stream");
        resp->addHeader("Content-Length", std::to_string(fileSize));
        resp->addHeader("Accept-Ranges", "bytes");
        resp->addHeader("Connection", "close");
        conn->setWriteCompleteCallback(
            [self = shared_from_this()](TcpConnectionPtr const &c) { c->shutdown(); });
        return true;
    }

    size_t startPos = 0, endPos = fileSize - 1;
    [[maybe_unused]] bool isRange = false;
    std::string rangeHdr = req.getHeader("Range");
    if (!rangeHdr.empty()) {
        std::regex rgx("bytes=(\\d+)-(\\d*)");
        std::smatch m;
        if (std::regex_search(rangeHdr, m, rgx)) {
            startPos = std::stoull(m[1]);
            endPos = (m[2].str().empty()) ? fileSize - 1 : std::stoull(m[2]);
            if (startPos >= fileSize) {
                utils::sendError(resp, "Range Not Satisfiable", 416, conn);
                return true;
            }
            if (endPos >= fileSize) {
                endPos = fileSize - 1;
            }
            isRange = true;
        }
    }

    auto httpCtx = std::static_pointer_cast<HttpContext>(conn->getContext());
    std::shared_ptr<FileDownContext> downCtx;
    if (httpCtx) {
        downCtx = httpCtx->getContext<FileDownContext>();
    }

    if (!downCtx) {
        downCtx = std::make_shared<FileDownContext>(filepath, originalFilename);
        if (httpCtx) {
            httpCtx->setContext(downCtx);
        }
        downCtx->seekTo(static_cast<long>(startPos));

        resp->setStatusCode(isRange ? HttpResponse::PartialContent : HttpResponse::OK);
        resp->setContentType("application/octet-stream");
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + originalFilename + "\"");
        resp->addHeader("Content-Length", std::to_string(endPos - startPos + 1));
        resp->addHeader("Accept-Ranges", "bytes");
        resp->addHeader("Connection", "keep-alive");
        if (isRange) {
            resp->addHeader("Content-Range",
                "bytes " + std::to_string(startPos) + "-" + std::to_string(endPos) + "/"
                    + std::to_string(fileSize));
        }
    }

    conn->setWriteCompleteCallback(
        [self = shared_from_this(), downCtx](TcpConnectionPtr const &conn) mutable {
            std::string chunk;
            if (downCtx->readNextChunk(chunk)) {
                conn->send(chunk);
            } else {
                conn->shutdown();
            }
        });
    return true;
}

bool ShareHandler::handleShareInfo(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string shareCode = req.getPathParam("code");
    if (shareCode.empty()) {
        utils::sendError(resp, "参数错误", 400, conn);
        return true;
    }

    auto rows = _db->queryParams(
        "SELECT fs.*, f.filename, f.original_filename, f.file_size, f.file_type, "
        "       u.username as owner_username, f.user_id "
        "FROM file_shares fs "
        "JOIN files f ON fs.file_id = f.id "
        "JOIN users u ON f.user_id = u.id "
        "WHERE fs.share_code = $1 AND (fs.expire_time IS NULL OR fs.expire_time > NOW())",
        {shareCode});

    if (rows.empty()) {
        utils::sendError(resp, "分享链接已失效或不存在", 404, conn);
        return true;
    }

    auto &row = rows[0];
    std::string shareType = row["share_type"];

    // 如果受保护且带有 extract_code 查询参数
    std::string extractParam = req.getQuery("extract_code", "");
    if (shareType == "protected") {
        if (extractParam.empty() || extractParam != row["extract_code"]) {
            utils::sendError(resp, "需要正确的提取码", 403, conn);
            return true;
        }
    }

    json fileInfo = {
        {"id", std::stoi(row["id"])},
        {"name", row["filename"]},
        {"originalName", row["original_filename"]},
        {"size", std::stoull(row["file_size"])},
        {"type", row["file_type"].empty() ? "unknown" : row["file_type"]},
        {"shareTime", row["created_at"]},
        {"expireTime", row["expire_time"]},
    };

    json respJson = {
        {"code", 0},
        {"message", "success"},
        {"shareType", shareType},
        {"file", fileInfo},
    };

    utils::sendJson(resp, respJson.dump(), 200, conn);
    return true;
}

bool ShareHandler::handleSearchUsers(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string sessionId = req.getHeader("X-Session-ID");
    int userId = 0;
    std::string username;
    if (!_sessions->validate(sessionId, userId, username)) {
        utils::sendError(resp, "未登录或会话已过期", 401, conn);
        return true;
    }

    std::string keyword = req.getQuery("keyword", "");
    if (keyword.empty()) {
        utils::sendError(resp, "搜索关键词不能为空", 400, conn);
        return true;
    }

    auto rows = _db->queryParams(
        "SELECT id,username,email FROM users WHERE username ILIKE $1 AND id!=$2 LIMIT 10;",
        {"%" + keyword + "%", std::to_string(userId)});

    std::string ss = "SELECT id,username,email FROM users WHERE username ILIKE '%" + keyword
                     + "%' AND id!=" + std::to_string(userId) + " LIMIT 10";
    json usersArr = json::array();
    for (auto &r: rows) {
        usersArr.push_back({
            {"id", std::stoi(r["id"])},
            {"username", r["username"]},
            {"email", r["email"].empty() ? "" : r["email"]},
        });
    }

    json result = {
        {"code", 0},
        {"message", "success"},
        {"users", usersArr},
    };

    utils::sendJson(resp, result.dump(), 200, conn);
    return true;
}
} // namespace handler
