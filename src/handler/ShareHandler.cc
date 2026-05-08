
#include "ShareHandler.h"
#include "db/Database.h"
#include "handler/SessionManager.h"
#include "utils/CodeGenerator.h"
#include <HttpRequest.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace handler {

using json = nlohmann::json;

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
        std::string shareType
            = data.at("shareType").get<std::string>(); // private/public/protected/user

        // 检查文件所有权
        auto fileRow = _db->queryOne("SELECT 1 FROM files WHERE id = $1 AND user_id = $2",
            {std::to_string(fileId), std::to_string(ownerId)});
        if (fileRow.empty()) {
            utils::sendError(resp, "您没有权限分享此文件", 403, conn);
            return true;
        }

        // 如果是取消分享 (private)
        if (shareType == "private") {
            _db->executeParams(
                "DELETE FROM file_shares WHERE file_id = $1", {std::to_string(fileId)});
            json result = {{"code", 0}, {"message", "文件已设为私有"}};
            utils::sendJson(resp, result.dump(), 200, conn);
            return true;
        }

        // 生成分享码
        std::string shareCode = utils::generateShareCode();
        std::string extractCode = "NULL";
        std::string sharedWithId = "NULL";

        // 处理不同类型的分享
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
            extractCode = "'" + utils::generateExtractCode() + "'";
        }

        // 过期时间处理
        std::string expireStr = "NULL";
        if (data.contains("expireTime") && !data["expireTime"].is_null()) {
            int hours = data["expireTime"].get<int>();
            if (hours > 0) {
                expireStr = "NOW() + INTERVAL '" + std::to_string(hours) + " hours'";
            }
        }

        // 插入分享记录
        std::string sql = "INSERT INTO file_shares (file_id, owner_id, shared_with_id, "
                          "share_type, share_code, extract_code, expire_time) VALUES ($1,$2,";
        if (sharedWithId == "NULL") {
            sql += "NULL,";
        } else {
            sql += sharedWithId + ",";
        }

        sql += "'" + shareType + "','" + shareCode + "',";
        if (extractCode == "NULL") {
            sql += "NULL,";
        } else {
            sql += extractCode + ",";
        }

        if (expireStr == "NULL") {
            sql += "NULL)";
        } else {
            sql += expireStr + ")";
        }

        // 使用参数化更安全，但这里为了简洁也可以直接拼接（已经做了转义）
        // 注意：实际建议完全参数化，此处为示例清晰起见用拼接，但 db_->executeParams 更好
        // 更好的写法是用参数化：
        // db_->executeParams("INSERT INTO file_shares (...) VALUES ($1,$2,$3,$4,$5,$6,$7)",
        //                    {fileId, ownerId, sharedWithId, shareType, shareCode, extractCode,
        //                    expireStr});
        // 为保持代码简洁，这里使用拼接（你的 escape 已做防护）
        // 但下面的实现改用 executeParams 示例：

        // 用参数化实现（推荐）
        int aff = _db->executeParams("INSERT INTO file_shares (file_id, owner_id, shared_with_id, "
                                     "share_type, share_code, extract_code, expire_time) "
                                     "VALUES ($1, $2, $3, $4, $5, $6, $7)",
            {std::to_string(fileId),
                std::to_string(ownerId),
                sharedWithId == "NULL" ? "" : sharedWithId, // 空字符串代表 NULL
                shareType,
                shareCode,
                extractCode == "NULL" ? ""
                                      : extractCode.substr(1, extractCode.length() - 2), // 去掉引号
                expireStr == "NULL" ? "" : expireStr});

        if (aff <= 0) {
            utils::sendError(resp, "创建分享失败", 500, conn);
            return true;
        }

        int shareId = _db->lastInsertId();
        json respJson = {{"code", 0},
            {"message", "分享成功"},
            {"shareId", shareId},
            {"shareType", shareType},
            {"shareCode", shareCode},
            {"shareLink", "/share/" + shareCode}};
        if (shareType == "protected") {
            respJson["extractCode"] = extractCode.substr(1, extractCode.length() - 2);
        }
        if (shareType == "user") {
            respJson["sharedWithId"] = std::stoi(sharedWithId);
        }

        utils::sendJson(resp, respJson.dump(), 200, conn);
        return true;
    } catch (std::exception const &e) {
        spdlog::error("[Share] 创建分享异常: {}", e.what());
        utils::sendError(resp, "请求处理失败", 500, conn);
        return true;
    }
}

bool ShareHandler::handleShareAccess(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
}

bool ShareHandler::handleShareDownload(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
}

bool ShareHandler::handleShareInfo(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
}

bool ShareHandler::handleSearchUsers(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
}
} // namespace handler
