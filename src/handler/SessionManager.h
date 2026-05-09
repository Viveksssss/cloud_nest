#pragma once

#include <memory>
#include <string>

class Database;

namespace handler {

class SessionManager{
public:
    /**
     * @param db 数据库接口（由外部注入）
     */
    explicit SessionManager(std::shared_ptr<Database> db);

    /**
     * @brief 创建新会话
     *
     * @param userId 用户 ID
     * @param username 用户名
     * @return 新生成的 sessionId（32 位随机字符串）
     *         如果创建失败，返回空字符串
     */
    std::string createSession(int userId, std::string const &username);

    /**
     * @brief 验证会话是否有效
     *
     * 若会话有效，会自动续期 30 分钟。
     *
     * @param sessionId 待验证的会话 ID
     * @param[out] userId 输出参数：用户 ID（验证成功时设置）
     * @param[out] username 输出参数：用户名（验证成功时设置）
     * @return true 表示会话有效，false 表示无效或已过期
     */
    bool validate(std::string const &sessionId, int &userId, std::string &username);

    /**
     * @brief 销毁会话（用户登出）
     *
     * @param sessionId 要销毁的会话 ID
     */
    void destroy(std::string const &sessionId);

private:
    std::shared_ptr<Database> _db;
    static constexpr int SESSION_EXPIRED_MINUTES = 30;
};

} // namespace handler
