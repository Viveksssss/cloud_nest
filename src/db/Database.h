// db/Database.h
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// 表示一行数据的便捷类型
using Row = std::unordered_map<std::string, std::string>;
using ResultSet = std::vector<Row>;

class Database {
public:
    virtual ~Database() = default;

    // ========== 查询 ==========
    /// 执行 INSERT/UPDATE/DELETE，返回影响行数（-1 表示失败）
    virtual int execute(std::string const &sql) = 0;

    /// 执行参数化 INSERT/UPDATE/DELETE
    virtual int executeParams(std::string const &sql, std::vector<std::string> const &params = {})
        = 0;

    /// 执行 SELECT，返回结果集
    virtual ResultSet query(std::string const &sql, std::vector<std::string> const &params = {})
        = 0;

    /// 执行参数化 SELECT
    virtual ResultSet queryParams(std::string const &sql, std::vector<std::string> const &params)
        = 0;

    /// 查询单行（不存在返回空 Row）
    virtual Row queryOne(std::string const &sql, std::vector<std::string> const &params) = 0;

    /// 查询单值（如 COUNT、lastval()）
    virtual std::string queryValue(std::string const &sql) = 0;

    // ========== 事务 ==========
    // （你的 ConnectionGuard 已经提供了事务基础）

    // ========== 安全 ==========
    /// SQL 转义（防注入）
    virtual std::string escape(std::string const &str) = 0;

    // ========== 工具 ==========
    /// 最后一次插入的 ID（PostgreSQL: SELECT lastval() / RETURNING id）
    virtual int lastInsertId() = 0;

    /// 检查连接是否可用
    virtual bool isConnected() = 0;
};
