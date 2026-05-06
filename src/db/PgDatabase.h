// db/PgDatabase.h
#pragma once
#include "Database.h"
#include "SqlPool.h"
#include <memory>

class PgDatabase : public Database {
public:
    /**
     * @brief 构造函数，接收外部创建的连接池
     *
     * @param pool 共享的 SqlPool
     */
    explicit PgDatabase(std::shared_ptr<SqlPool> pool);

    // Database 接口实现
    int execute(std::string const &sql) override;
    int executeParams(std::string const &sql, std::vector<std::string> const &params) override;
    ResultSet query(std::string const &sql) override;
    ResultSet queryParams(std::string const &sql, std::vector<std::string> const &params) override;
    Row queryOne(std::string const &sql) override;
    std::string queryValue(std::string const &sql) override;
    std::string escape(std::string const &str) override;
    int lastInsertId() override;
    bool isConnected() override;

private:
    std::shared_ptr<SqlPool> pool_;
};
