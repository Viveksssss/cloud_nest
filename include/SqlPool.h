#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <noncopyable.h>
#include <pqxx/pqxx>
#include <queue>
#include <string>
#include <sys/cdefs.h>
#include <thread>

class SqlPool : noncopyable {
public:
    /**
     * @brief Construct a new Sql Pool object
     *
     * @param host
     * @param dbname
     * @param user
     * @param password
     * @param max_size
     * @param min_size
     */
    SqlPool(std::string const &host, std::string const dbname, std::string const &user,
        std::string const password, size_t max_size = 16, size_t min_size = 4);
    /**
     * @brief Destroy the Sql Pool object
     *
     */
    ~SqlPool();

    /**
     * @brief Get the Connection object,wait up to 5s by default
     *
     * @param timeout_ms
     * @return std::shared_ptr<pqxx::connection>
     */
    std::shared_ptr<pqxx::connection> getConnection(int timeout_ms = 5000);
    /**
     * @brief Return connection to pool
     *
     * @param conn
     */
    void returnConnection(std::shared_ptr<pqxx::connection> conn);

    /**
     * @brief Get the Connection String object
     *
     * @return std::string
     */
    __attribute__((always_inline)) std::string getConnectionString() const {
        return _conn_str;
    }

    /**
     * @brief RAII-style connection wrapper
     *
     */
    class ConnectionGuard : noncopyable {
    private:
        SqlPool &_pool;
        std::shared_ptr<pqxx::connection> _conn;
        bool _released;

    public:
        /**
         * @brief Construct a new Connection Guard object
         *
         * @param pool
         * @param timeout_ms
         */
        ConnectionGuard(SqlPool &pool, int timeout_ms = 5000);
        /**
         * @brief Destroy the Connection Guard object
         *
         */
        ~ConnectionGuard();
        /**
         * @brief Truly return the connection to the pool
         *
         */
        void release();
        /**
         * @brief Construct a new Connection Guard object
         *
         * @param other
         */
        ConnectionGuard(ConnectionGuard &&other) noexcept;
        ConnectionGuard &operator=(ConnectionGuard &&other) noexcept;
        /**
         * @brief Get connection reference
         *
         * @return pqxx::connection&
         */
        pqxx::connection &operator*();
        /**
         * @brief Get connection pointer
         *
         * @return pqxx::connection*
         */
        pqxx::connection *operator->();
        /**
         * @brief Get raw connection pointer
         *
         * @return std::shared_ptr<pqxx::connection>
         */
        std::shared_ptr<pqxx::connection> get();
    };

private:
    /**
     * @brief Create a new Connection object
     *
     * @return std::shared_ptr<pqxx::connection>
     */
    std::shared_ptr<pqxx::connection> createConnection();
    /**
     * @brief Maintenance thread: Regularly check and maintain the minimum number of connections
     *
     */
    void maintenanceWorker();

private:
    std::string _conn_str;
    std::queue<std::shared_ptr<pqxx::connection>> _pool;
    std::mutex _mtx;
    std::condition_variable _cv;
    size_t _max_size;
    size_t _min_size;
    size_t _active_connections;
    bool _is_running;
    std::thread _thread;
};
