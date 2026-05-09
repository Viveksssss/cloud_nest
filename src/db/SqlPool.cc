#include "SqlPool.h"
#include <chrono>
#include <exception>
#include <mutex>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

SqlPool::SqlPool(std::string const &host, std::string const dbname, std::string const &user,
    std::string const password, size_t max_size, size_t min_size)
    : _is_running(true)
    , _active_connections(0)
    , _max_size(max_size)
    , _min_size(min_size) {
    _conn_str = "host=" + host + " dbname=" + dbname + " user=" + user + " password=" + password
                + " client_encoding=UTF8";

    spdlog::info(
        "[初始化] 正在创建连接池...\n[配置] 最大连接数: {} ,最小连接数:{}", max_size, min_size);

    std::lock_guard<std::mutex> lock(_mtx);
    for (size_t i = 0; i < _min_size; ++i) {
        try {
            auto conn = createConnection();
            _pool.push(conn);
        } catch (std::exception &e) {
            spdlog::error("[错误] 初始化连接池失败: {}", e.what());
            _is_running = false;
            throw;
        }
    }

    _thread = std::thread(&SqlPool::maintenanceWorker, this);
    spdlog::info("[初始化] 连接池创建完成，初始大小: {}", _pool.size());
}

SqlPool::~SqlPool() {
    spdlog::info("[关闭] 正在关闭连接池...");
    _is_running = false;
    _cv.notify_all();
    if (_thread.joinable()) {
        _thread.join();
    }

    std::lock_guard<std::mutex> lock(_mtx);
    int closed_count = 0;
    while (!_pool.empty()) {
        closed_count++;
        _pool.pop();
    }
    spdlog::info("[关闭] 释放 {} 个连接", closed_count);
}

std::shared_ptr<pqxx::connection> SqlPool::getConnection(int timeout_ms) {
    std::unique_lock<std::mutex> lock(_mtx);

    if (_pool.empty() && _active_connections >= _max_size) {
        spdlog::info("[等待] 连接池已满，等待可用连接...");

        auto timeout = std::chrono::milliseconds(timeout_ms);
        if (!_cv.wait_for(lock, timeout, [this] { return !_pool.empty() || !_is_running; })) {
            spdlog::warn("[超时] 获取连接超时 ({})ms", std::to_string(timeout_ms));
            return {};
        }
        if (!_is_running) {
            throw std::runtime_error("[错误] 连接池已关闭");
        }
    }

    if (!_pool.empty()) {
        auto conn = _pool.front();
        _pool.pop();
        _active_connections++;

        try {
            if (conn->is_open()) {
                spdlog::debug("[获取] 从连接池获取连接，剩余空闲:{} , 活跃: {}",
                    _pool.size(),
                    _active_connections);
                return conn;
            } else {
                spdlog::error("[警告] 连接已断开，创建新连接");
                conn = createConnection();
                return conn;
            }
        } catch (std::exception const &e) {
            spdlog::error("[错误] 连接无效: {}", e.what());
            _active_connections--; // 减少计数
            conn = createConnection();
            _active_connections++;
            return conn;
        }
    }

    if (_active_connections < _max_size) {
        auto conn = createConnection();
        _active_connections++;
        return conn;
    }

    throw std::runtime_error("[错误] 无法获取连接");
}

void SqlPool::returnConnection(std::shared_ptr<pqxx::connection> conn) {
    if (!conn) {
        spdlog::warn("[警告] 尝试归还空连接");
        return;
    }

    std::lock_guard<std::mutex> lock(_mtx);
    try {
        if (conn->is_open()) {
            _pool.push(conn);
            _active_connections--;
            spdlog::debug(
                "[归还] 连接已归还，空闲: {} ,活跃: {}", _pool.size(), _active_connections);
        } else {
            _active_connections--;
            spdlog::info("[丢弃] 连接已关闭，已丢弃");
        }
    } catch (std::exception const &e) {
        _active_connections--;
        spdlog::error("[错误] 归还连接失败: ", e.what());
    }

    _cv.notify_one();
}

std::shared_ptr<pqxx::connection> SqlPool::createConnection() {
    try {
        auto conn = std::make_shared<pqxx::connection>(_conn_str);
        conn->set_client_encoding("UTF8");
        spdlog::info("[创建连接] 新连接已创建，当前活跃连接数: {}", _active_connections + 1);
        return conn;
    } catch (std::exception const &e) {
        spdlog::error("[错误] 创建连接失败: {}", e.what());
        throw;
    }
}

void SqlPool::maintenanceWorker() {
    while (_is_running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::lock_guard<std::mutex> lock(_mtx);
        try {
            while (_pool.size() + _active_connections < _min_size && _is_running) {
                auto conn = createConnection();
                _pool.push(conn);
                spdlog::info("[维护] 补充空闲连接，池大小: {}", _pool.size());
            }
        } catch (std::exception const &e) {
            spdlog::error("[维护] 创建连接失败: {}", e.what());
        }
    }
}

SqlPool::ConnectionGuard::ConnectionGuard(SqlPool &pool, int timeout_ms)
    : _pool(pool)
    , _released(false)
    , _conn(nullptr) {
    _conn = _pool.getConnection();
    _conn->set_client_encoding("UTF8");
}

SqlPool::ConnectionGuard::ConnectionGuard(std::shared_ptr<SqlPool> pool, int timeout_ms)
    : _pool(*pool)
    , _released(false)
    , _conn(nullptr) {
    _conn = _pool.getConnection();
    _conn->set_client_encoding("UTF8");
}

SqlPool::ConnectionGuard::~ConnectionGuard() {
    release();
}

void SqlPool::ConnectionGuard::release() {
    if (!_released && _conn) {
        _pool.returnConnection(_conn);
        _released = true;
        _conn = nullptr;
    }
}

SqlPool::ConnectionGuard::ConnectionGuard(ConnectionGuard &&other) noexcept
    : _pool(other._pool)
    , _conn(std::move(other._conn))
    , _released(other._released) {
    other._released = true;
    other._conn = nullptr;
}

SqlPool::ConnectionGuard &SqlPool::ConnectionGuard::operator=(ConnectionGuard &&other) noexcept {
    if (this != &other) {
        release();
        _conn = std::move(other._conn);
        _released = other._released;
        other._released = true;
        other._conn = nullptr;
    }
    return *this;
}

pqxx::connection &SqlPool::ConnectionGuard::operator*() {
    if (!_conn) {
        throw std::runtime_error("连接已释放");
    }
    return *_conn;
}

pqxx::connection *SqlPool::ConnectionGuard::operator->() {
    if (!_conn) {
        throw std::runtime_error("连接已释放");
    }
    return _conn.get();
}

std::shared_ptr<pqxx::connection> SqlPool::ConnectionGuard::get() {
    return _conn;
}
