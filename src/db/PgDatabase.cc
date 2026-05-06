// db/PgDatabase.cc
#include "PgDatabase.h"
#include "SqlPool.h"
#include <spdlog/spdlog.h>

PgDatabase::PgDatabase(std::shared_ptr<SqlPool> pool) : pool_(pool) {
}

// ========== 执行 SQL ==========

int PgDatabase::execute(std::string const &sql) {
    return executeParams(sql, {});
}

int PgDatabase::executeParams(std::string const &sql, std::vector<std::string> const &params) {
    try {
        auto guard = SqlPool::ConnectionGuard(pool_);
        pqxx::work txn(*guard);
        pqxx::result res = txn.exec(sql, pqxx::params(params));
        txn.commit();
        return res.affected_rows();
    } catch (std::exception const &e) {
        spdlog::error("[SQL] 参数化执行失败: {}\n语句: {}", e.what(), sql);
        return -1;
    }
}

// ========== 查询 ==========

ResultSet PgDatabase::query(std::string const &sql) {
    ResultSet result;
    try {
        auto guard = SqlPool::ConnectionGuard(pool_);
        pqxx::work txn(*guard);
        pqxx::result res = txn.exec(sql);
        txn.commit();

        for (auto const &row: res) {
            Row r;
            for (auto const &field: row) {
                r[field.name()] = field.is_null() ? "" : field.c_str();
            }
            result.push_back(std::move(r));
        }
    } catch (std::exception const &e) {
        spdlog::error("[SQL] 查询失败: {}", e.what());
    }
    return result;
}

ResultSet PgDatabase::queryParams(std::string const &sql, std::vector<std::string> const &params) {
    ResultSet result;
    try {
        auto guard = SqlPool::ConnectionGuard(pool_);
        pqxx::work txn(*guard);
        pqxx::result res = txn.exec(sql, pqxx::params(params));
        txn.commit();

        for (auto const &row: res) {
            Row r;
            for (auto const &field: row) {
                r[field.name()] = field.is_null() ? "" : field.c_str();
            }
            result.push_back(std::move(r));
        }
    } catch (std::exception const &e) {
        spdlog::error("[SQL] 参数化查询失败: {}", e.what());
    }
    return result;
}

Row PgDatabase::queryOne(std::string const &sql) {
    auto result = query(sql);
    return result.empty() ? Row{} : result[0];
}

std::string PgDatabase::queryValue(std::string const &sql) {
    auto result = query(sql);
    if (result.empty() || result[0].empty()) {
        return "";
    }
    return result[0].begin()->second;
}

// ========== 安全 ==========

std::string PgDatabase::escape(std::string const &str) {
    // pqxx 的参数化查询已经防注入，这里提供手动转义备用
    // 使用 PostgreSQL 的转义规则
    auto guard = SqlPool::ConnectionGuard(pool_);
    return guard->esc(str); // pqxx::connection 内置方法
}

// ========== 工具 ==========

int PgDatabase::lastInsertId() {
    try {
        auto guard = SqlPool::ConnectionGuard(pool_);
        pqxx::work txn(*guard);
        pqxx::result res = txn.exec("SELECT lastval()");
        txn.commit();
        if (!res.empty() && !res[0][0].is_null()) {
            return res[0][0].as<int>();
        }
    } catch (std::exception const &e) {
        spdlog::error("[SQL] 获取 LASTVAL 失败: {}", e.what());
    }
    return -1;
}

bool PgDatabase::isConnected() {
    try {
        auto guard = SqlPool::ConnectionGuard(pool_);
        return guard->is_open();
    } catch (...) {
        return false;
    }
}
