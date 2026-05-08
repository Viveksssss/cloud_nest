#include "SessionManager.h"
#include "db/Database.h"
#include "utils/CodeGenerator.h"
#include <memory>
#include <spdlog/spdlog.h>
#include <string>

// TODO:改进:会话管理使用Redis
namespace handler {

SessionManager::SessionManager(std::shared_ptr<Database> db) : _db(std::move(db)) {
    spdlog::info("[Session] 会话管理器初始化完成");
}

std::string SessionManager::createSession(int userId, std::string const &username) {
    if (!_db->isConnected()) {
        spdlog::error("[Session] 数据库未连接,无法创建会话");
        return "";
    }

    std::string sessionId = utils::generateSessionId();
    std::string sql = "INSERT INTO sessions (session_id,user_id,username,expire_time) VALUES "
                      "($1,$2,$3,NOW() + INTERVAL '"
                      + std::to_string(SESSION_EXPIRED_MINUTES) + " minutes')";

    int affected = _db->executeParams(sql, {sessionId, std::to_string(userId), username});
    if (affected > 0) {
        spdlog::info("[Session] 创建会话成功: userId={}, username={}, sessionId={}",
            userId,
            username,
            sessionId);
        return sessionId;
    } else {
        spdlog::error("[Session] 创建会话失败: userId={}", userId);
        return "";
    }
}

bool SessionManager::validate(std::string const &sessionId, int &userId, std::string &username) {
    if (sessionId.empty()) {
        spdlog::warn("[Session] 验证失败: sessionId 为空");
        return false;
    }

    if (!_db->isConnected()) {
        spdlog::error("[Session] 数据库未连接，无法验证会话");
        return false;
    }

    std::string querySql = "SELECT user_id, username FROM sessions "
                           "WHERE session_id = $1 AND expire_time > NOW()";

    Row row = _db->queryOne(querySql, {sessionId});
    if (row.empty()) {
        spdlog::debug("[Session] 会话无效或已过期: sessionId={}", sessionId);
        return false;
    }

    userId = std::stoi(row["user_id"]);
    username = row["username"];
    std::string updateSql = "UPDATE sessions SET expire_time = NOW() + INTERVAL '"
                            + std::to_string(SESSION_EXPIRED_MINUTES)
                            + "minutes' WHERE session_id = $1";

    int affected = _db->executeParams(updateSql, {sessionId});
    if (affected > 0) {
        spdlog::debug("[Session] 会话续期成功: userId={}, sessionId={}", userId, sessionId);
    } else {
        spdlog::warn("[Session] 会话续期失败（可能已被删除）: sessionId={}", sessionId);
    }
    return true;
}

void SessionManager::destroy(std::string const &sessionId) {
    if (sessionId.empty()) {
        spdlog::warn("[Session] 销毁失败: sessionId 为空");
        return;
    }

    if (!_db->isConnected()) {
        spdlog::error("[Session] 数据库未连接，无法销毁会话");
        return;
    }
    std::string sql = "DELETE FROM sessions WHERE session_id = $1";
    int affected = _db->executeParams(sql, {sessionId});

    if (affected > 0) {
        spdlog::info("[Session] 会话已销毁: sessionId={}", sessionId);
    } else {
        spdlog::debug("[Session] 销毁会话时未找到记录: sessionId={}", sessionId);
    }
}

} // namespace handler
