#pragma once

#include "EventLoopThreadPool.h"
#include "HttpServer.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <random>
#include <regex>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace clouenest {

namespace fs = std::filesystem;

class FileUploadContext;
class FileDownContext;

class HttpUploadHandler {
public:
    /**
     * @brief Construct a new Http Upload Handler object
     *
     * @param numThreads
     * @param dbHost
     * @param dbUser
     * @param dbPasswd
     * @param dbName
     * @param dbPort
     */
    HttpUploadHandler(int numThreads, std::string const &dbHost = "127.0.0.1",
        std::string const &dbUser = "postgres", std::string const &dbPasswd = "postgres",
        std::string const &dbName = "file_manager", unsigned int dbPort = 5432);

    /**
     * @brief Destroy the Http Upload Handler object
     *
     */
    ~HttpUploadHandler();

    /**
     * @brief Http Request callback,true:process success,false:need more
     *
     * @param conn
     * @param req
     * @param resp
     */
    void onRequest(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

    /**
     * @brief Connection callback
     *
     */
    void onConnection();

private:
    /**
     * @brief
     *
     */
    void registerRoute();

private:
    EventLoopThreadPool _threadPool;
    std::string _uploadDir;
    std::string _mappingFile;
    std::atomic<int> _activeRequests;
    std::mutex _mappingMutex;
    std::unordered_map<std::string, std::string> _filenameMapping;
};

} // namespace clouenest
