#pragma once

#include <Callbacks.h>
#include <memory>
#include <string>

class HttpRequest;
class HttpResponse;
class Database;

namespace handler {

class SessionManager;

class FileHandler : std::enable_shared_from_this<FileHandler> {
public:
    FileHandler(std::shared_ptr<Database> db, std::shared_ptr<SessionManager> sessions,
        std::string const &uploadDir);

    // 文件上传 (POST /upload)
    bool handlerUpload(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);
    // 文件下载 (GET\head /download/{filename})
    bool handleDownload(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);
    // 文件列表 (GET /files?type=my|shared|all&keyword=...)
    bool handleListFiles(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);
    // 文件删除 (DELETE /delete/{filename})
    bool handleDelete(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp);

private:
    bool checkOwnership(int fileId, int userId);
    bool checkDownloadPermission(std::string const &filename, std::string const &sessionId,
        std::string const &shareCode, std::string const &extractCode, int &userIdOut,
        std::string &originalFilename, std::string &serverFilename, std::string &errorMsg);

private:
    std::shared_ptr<Database> _db;
    std::shared_ptr<SessionManager> _sessions;
    std::string _uploadDir;
};

}; // namespace handler
