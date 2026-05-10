#include "FileHandler.h"
#include "db/Database.h"
#include "handler/FileDownContext.h"
#include "handler/FileUploadContext.h"
#include "SessionManager.h"
#include "utils/CodeGenerator.h"
#include "utils/FileUtils.h"
#include "utils/StringUtils.h"
#include <Callbacks.h>
#include <cstdint>
#include <filesystem>
#include <HttpContext.h>
#include <HttpRequest.h>
#include <HttpResponse.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
#include <TcpConnection.h>
#include <unistd.h>

namespace handler {

namespace fs = std::filesystem;
using json = nlohmann::json;

FileHandler::FileHandler(std::shared_ptr<Database> db, std::shared_ptr<SessionManager> sessions,
    std::string const &uploadDir)
    : _db(std::move(db))
    , _sessions(std::move(sessions))
    , _uploadDir(uploadDir) {
    try {
        fs::create_directories(_uploadDir);
    } catch (...) {
        spdlog::error("创建上传目录失败: {}", _uploadDir);
    }
}

bool FileHandler::handleUpload(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    // 1. 验证登录
    std::string sessionId = req.getHeader("X-Session-ID");
    int userId = 0;
    std::string username;
    if (!_sessions->validate(sessionId, userId, username)) {
        utils::sendError(resp, "未登录或会话已过期", 401, conn);
        return true;
    }

    // 2. 获取或创建上传上下文
    auto httpCtx = std::static_pointer_cast<HttpContext>(conn->getContext());
    if (!httpCtx) {
        utils::sendError(resp, "服务器内部错误", 500, conn);
        return true;
    }

    std::shared_ptr<FileUploadContext> upCtx = httpCtx->getContext<FileUploadContext>();

    // ========== 首次处理：解析头部，创建上下文 ==========
    if (!upCtx) {
        std::string contentType = req.getHeader("Content-Type");
        std::regex boundaryRe("boundary=(.+)$");
        std::smatch match;
        if (!std::regex_search(contentType, match, boundaryRe)) {
            utils::sendError(resp, "无效的 Content-Type", 400, conn);
            return true;
        }
        std::string boundary = "--" + match[1].str();

        // 获取原始文件名
        std::string originalName;
        std::string headerName = req.getHeader("X-File-Name");
        if (!headerName.empty()) {
            originalName = utils::urlDecode(headerName);
        } else {
            // 从 body 中的 Content-Disposition 头提取
            std::string body = req.body();
            std::regex fnRe(R"(Content-Disposition:.*filename=\"([^\"]+)\")");
            if (std::regex_search(body, match, fnRe) && match[1].matched) {
                originalName = match[1].str();
            } else {
                originalName = "unknown_file";
            }
        }

        std::string serverFilename = utils::generateUniqueFilename("upload");
        std::string filepath = _uploadDir + "/" + serverFilename;

        upCtx = std::make_shared<FileUploadContext>(filepath, originalName);
        upCtx->setBoundary(boundary);
        httpCtx->setContext(upCtx);

        // 处理第一个数据块
        std::string body = req.body(); // 此时 body 可能已经包含部分文件数据
        size_t headerEnd = body.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            utils::sendError(resp, "请求格式错误", 400, conn);
            return true;
        }
        headerEnd += 4; // 跳过 multipart 头部，指向文件内容开始

        std::string endBoundary = boundary + "--";

        size_t endPos = body.find(endBoundary);
        if (endPos != std::string::npos) {
            // 本包已包含结束边界，上传在本次完成
            if (endPos > headerEnd) {
                upCtx->writeData(body.data() + headerEnd, endPos - headerEnd);
            }
            upCtx->setState(FileUploadContext::State::Complete);
        } else {
            // 未找到结束边界，写入全部文件内容（从 headerEnd 到末尾）
            if (body.size() > headerEnd) {
                upCtx->writeData(body.data() + headerEnd, body.size() - headerEnd);
            }
            upCtx->setState(FileUploadContext::State::ExpectBounday);
        }

        req.setBody("");
    }
    // ========== 后续数据包处理 ==========
    else {
        std::string body = req.body();
        // 新到达且未被处理过的数据视图
        std::string_view newData(body.data(), body.size());
        std::string boundary = upCtx->getBoundary();
        std::string endBoundary = boundary + "--";
        bool completed = false;

        switch (upCtx->getState()) {
        case FileUploadContext::State::ExpectBounday: {
            // 优先查找结束边界
            size_t endPos = newData.find(endBoundary);
            if (endPos != std::string_view::npos) {
                if (endPos > 0) {
                    upCtx->writeData(newData.data(), endPos);
                }
                completed = true;
                req.setBody("");
                break;
            }

            // 查找普通边界（通常用于多段上传，这里我们假设只有一个文件段）
            size_t nextBoundary = newData.find(boundary);
            if (nextBoundary != std::string_view::npos) {
                // 写入边界之前的数据
                if (nextBoundary > 0) {
                    upCtx->writeData(newData.data(), nextBoundary);
                }
                // 跳过边界行和可能存在的子头部（例如 \r\nContent-Disposition...）
                size_t subHeaderEnd = newData.find("\r\n\r\n", nextBoundary);
                if (subHeaderEnd != std::string_view::npos) {
                    subHeaderEnd += 4;
                    // 对剩余部分递归处理：检查是否立即包含结束边界
                    std::string_view rest = newData.substr(subHeaderEnd);
                    size_t endInRest = rest.find(endBoundary);
                    if (endInRest != std::string_view::npos) {
                        if (endInRest > 0) {
                            upCtx->writeData(rest.data(), endInRest);
                        }
                        completed = true;
                    } else {
                        // 没有结束边界，写入所有剩余内容
                        if (!rest.empty()) {
                            upCtx->writeData(rest.data(), rest.size());
                        }
                        upCtx->setState(FileUploadContext::State::ExpectContent);
                    }
                    req.setBody("");
                } else {
                    // 子头部不完整，无法继续，当前包不做处理，等待更多数据
                    break;
                }
            } else {
                // 没有任何边界，直接全部写入
                upCtx->writeData(newData.data(), newData.size());
                req.setBody("");
            }
            break;
        }

        case FileUploadContext::State::ExpectContent: {
            size_t endPos = newData.find(endBoundary);
            if (endPos != std::string_view::npos) {
                if (endPos > 0) {
                    upCtx->writeData(newData.data(), endPos);
                }
                completed = true;
            } else {
                upCtx->writeData(newData.data(), newData.size());
            }
            req.setBody("");
            break;
        }

        default: break;
        }

        if (completed) {
            upCtx->setState(FileUploadContext::State::Complete);
        }
    }

    // ========== 检查是否上传完成 ==========
    if (upCtx->getState() == FileUploadContext::State::Complete || httpCtx->gotAll()) {
        uintmax_t size = upCtx->getTotalBytes();
        std::string serverFilename = fs::path(upCtx->getFilename()).filename().string();
        std::string fileType = utils::getFileType(upCtx->getOriginalFilename());

        auto res = _db->queryParams(
            "INSERT INTO files (filename,original_filename,file_size,file_type,user_id) VALUES "
            "($1,$2,$3,$4,$5) RETURNING id",
            {serverFilename,
                upCtx->getOriginalFilename(),
                std::to_string(size),
                fileType,
                std::to_string(userId)});
        if (res.empty()) {
            spdlog::error("文件信息存储失败!");
            utils::sendError(resp, "文件信息存储失败", 500, conn);
            httpCtx->setContext(nullptr);
            return true;
        }

        int fileId = std::stoi(res[0]["id"]);
        json respJson = {{"code", 0},
            {"message", "上传成功"},
            {"fileId", fileId},
            {"filename", serverFilename},
            {"originalFilename", upCtx->getOriginalFilename()},
            {"size", size}};

        utils::sendJson(resp, respJson.dump(), 200, conn);
        httpCtx->setContext(nullptr);
        return true;
    }

    // 尚未接收完成，继续等待
    return false;
}

bool FileHandler::handleDownload(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string filename = req.getPathParam("filename");
    if (filename.empty()) {
        utils::sendError(resp, "缺少文件名", 400, conn);
        return true;
    }

    std::string sessionId = req.getHeader("X-Session-ID");
    if (sessionId.empty()) {
        sessionId = req.getQuery("sessionId", "");
    }
    std::string shareCode = req.getQuery("code", "");
    std::string extractCode = req.getQuery("extract_code", "");

    int userId = 0;
    std::string originalFilename, serverFilename, errorMsg;
    if (!checkDownloadPermission(filename,
            sessionId,
            shareCode,
            extractCode,
            userId,
            originalFilename,
            serverFilename,
            errorMsg)) {
        int code = (errorMsg == "清先登录") ? 401 : 403;
        utils::sendError(resp, errorMsg, code, conn);
        return true;
    }

    std::string filepath = _uploadDir + "/" + serverFilename;
    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        utils::sendError(resp, "文件未找到", 404, conn);
        return true;
    }

    uintmax_t fileSize = fs::file_size(filepath);
    if (req.method() == HttpRequest::Method::Head) {
        resp->setStatusCode(HttpResponse::OK);
        resp->setContentType("application/octet-stream");
        resp->addHeader("Content-Length", std::to_string(fileSize));
        resp->addHeader("Accept-Ranges", "bytes");
        resp->addHeader("Connection", "close");
        conn->setWriteCompleteCallback(
            [self = shared_from_this()](TcpConnectionPtr const &c) { c->shutdown(); });
        return true;
    }

    long long startPos = 0, endPos = static_cast<long long>(fileSize - 1);
    [[maybe_unused]] bool isRange = false;
    std::string rangeHdr = req.getHeader("Range");
    if (!rangeHdr.empty()) {
        std::regex rgx("bytes=(\\d+)-(\\d*)");
        std::smatch match;
        if (std::regex_search(rangeHdr, match, rgx)) {
            startPos = std::stoll(match[1].str());
            if (match[2].str().empty()) {
                endPos = static_cast<long long>(fileSize - 1);
            } else {
                endPos = static_cast<long long>(std::stoll(match[2].str()));
            }
            if (startPos >= static_cast<long long>(fileSize)) {
                utils::sendError(resp, "Range Now Satisfiable", 416, conn);
                return true;
            }
            if (endPos >= static_cast<long long>(fileSize)) {
                endPos = static_cast<long long>(fileSize - 1);
            }
            isRange = true;
        }
    }

    auto httpCtx = std::static_pointer_cast<HttpContext>(conn->getContext());
    std::shared_ptr<FileDownContext> downCtx;
    if (httpCtx) {
        downCtx = httpCtx->getContext<FileDownContext>();
    }
    if (!downCtx) {
        downCtx = std::make_shared<FileDownContext>(filepath, originalFilename);
        if (httpCtx) {
            httpCtx->setContext(downCtx);
        }

        downCtx->seekTo(startPos);
        resp->setStatusCode(isRange ? HttpResponse::PartialContent : HttpResponse::OK);
        resp->setContentType("application/octet-stream");
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + originalFilename + "\"");
        resp->addHeader("Accept-Ranges", "bytes");
        resp->addHeader("Content-Length", std::to_string(endPos - startPos + 1));
        resp->addHeader("Connection", "keep-alive");
        if (isRange) {
            resp->addHeader("Content-Range",
                "bytes " + std::to_string(startPos) + "-" + std::to_string(endPos) + "/"
                    + std::to_string(fileSize));
        }
    }

    conn->setWriteCompleteCallback(
        [self = shared_from_this(), downCtx](TcpConnectionPtr const &conn) mutable {
            std::string chunk;
            if (downCtx->readNextChunk(chunk)) {
                conn->send(chunk);
            } else {
                conn->shutdown();
            }
        });
    return true;
}

bool FileHandler::handleListFiles(
    TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string sessionId = req.getHeader("X-Session-ID");
    int userId = 0;
    std::string username;
    if (!_sessions->validate(sessionId, userId, username)) {
        utils::sendError(resp, "未登录或会话已过期", 401, conn);
        return true;
    }

    std::string type = req.getQuery("type", "my");
    std::string keyword = req.getQuery("keyword", "");
    std::string sql;

    if (type == "my") {
        sql = "SELECT f.id ,f.filename ,f.original_filename,f.file_size,f.file_type,"
              "f.created_at, 1 AS is_owner FROM files f WHERE f.user_id = $1";
        if (!keyword.empty()) {
            sql += " AND f.original_filename ILIKE '%' || $2 || '%' ";
        }
    } else if (type == "shared") {
        sql = "SELECT f.id,f.filename,f.original_filename,f.file_size,f.file_type,"
              "f.created_at,0 AS is_owner FROM files f JOIN file_shares fs ON f.id = fs.file_id "
              "WHERE (fs.shared_with_id = $1 OR fs.share_type = 'public') AND f.user_id != $1";
        if (!keyword.empty()) {
            sql += " AND f.original_filename ILIKE '%' || $2 || '%'";
        }
    } else {
        sql = "SELECT DISTINCT f.id,f.filename,f.original_filename,f.file_size,f.file_type,"
              "f.created_at,CASE WHEN f.user_id = $1 THEN 1 ELSE 0 END AS is_owner "
              "FROM files f LEFT JOIN file_shares fs ON f.id = fs.file_id "
              "WHERE (f.user_id = $1 OR fs.shared_with_id = $1 OR fs.share_type = 'public') "
              "AND (f.user_id = $1 fs.expire_time IS NULL OR fs.expire_time > NOW())";
        if (!keyword.empty()) {
            sql += " AND f.original_filename ILIKE '%' || $2 || '%'";
        }
    }

    ResultSet rows;
    if (keyword.empty()) {
        rows = _db->queryParams(sql, {std::to_string(userId)});
    } else {
        rows = _db->queryParams(sql, {std::to_string(userId), keyword});
    }

    json filesArr = json::array();
    for (auto &row: rows) {
        int fileId = std::stoi(row["id"]);
        bool isOwner = (std::stoi(row["is_owner"]) == 1);
        // 文件的基本信息
        json fileObj = {
            {"id", fileId},
            {"name", row["filename"]},
            {"originalName", row["original_filename"]},
            {"size", std::stoull(row["file_size"])},
            {"type", row["file_type"].empty() ? "unknown" : row["file_type"]},
            {"createdAt", row["created_at"]},
            {"isOwner", isOwner},
        };
        // 如果是所有者添加额外的文件信息
        if (isOwner) {
            auto shareRows
                = _db->queryParams("SELECT share_type, share_code, extract_code, expire_time "
                                   "FROM file_shares WHERE file_id = $1",
                    {std::to_string(fileId)});
            if (!shareRows.empty()) {
                auto &sr = shareRows[0];
                json shareInfo = {{"type", sr["share_type"]}, {"shareCode", sr["share_code"]}};
                if (sr["share_type"] == "protected" && !sr["extract_code"].empty()) {
                    shareInfo["extractCode"] = sr["extract_code"];
                }
                if (!sr["expire_time"].empty()) {
                    shareInfo["expireTime"] = sr["expire_time"];
                }
                fileObj["shareInfo"] = shareInfo;
            }
        }
        filesArr.push_back(fileObj);
    }

    json result = {{"code", 0}, {"message", "success"}, {"files", filesArr}};
    utils::sendJson(resp, result.dump(), 200, conn);

    return true;
}

bool FileHandler::handleDelete(TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
    std::string sessionId = req.getHeader("X-Session-ID");
    int userId = 0;
    std::string username;
    if (!_sessions->validate(sessionId, userId, username)) {
        utils::sendError(resp, "未登录或会话已过期", 401, conn);
        return true;
    }

    std::string filename = req.getPathParam("filename");
    if (filename.empty()) {
        utils::sendError(resp, "缺少文件名", 400, conn);
        return true;
    }

    auto row = _db->queryOne("SELECT id FROM files WHERE filename = $1 AND user_id = $2",
        {filename, std::to_string(userId)});

    if (row.empty()) {
        utils::sendError(resp, "文件不存在或者无权删除", 403, conn);
        return true;
    }
    int fieId = std::stoi(row["id"]);

    // 1.cpp
    std::string filepath = _uploadDir + "/" + filename;
    std::error_code ec;
    if (fs::exists(filepath, ec)) {
        fs::remove(filepath, ec);
        if (ec) {
            spdlog::warn("删除文件失败: {}", filepath);
            return true;
        }
    }
    // 2.posix
    // if (access(filepath.c_str(), F_OK) == 0) {
    //     if (unlink(filepath.c_str()) != 0) {
    //         spdlog::warn("删除文件失败: {}", filepath);
    //     }
    // }

    _db->executeParams("DELETE FROM files WHERE id = $1", {std::to_string(fieId)});
    json result = {{"code", 0}, {"message", "删除成功"}};
    utils::sendJson(resp, result.dump(), 200, conn);

    return true;
}

bool FileHandler::checkOwnership(int fileId, int userId) {
    auto row = _db->queryOne("SELECT 1 FROM files WHERE id = $1 AND user_id = $2",
        {std::to_string(fileId), std::to_string(userId)});
    return !row.empty();
}

bool FileHandler::checkDownloadPermission(std::string const &filename, std::string const &sessionId,
    std::string const &shareCode, std::string const &extractCode, int &userIdOut,
    std::string &originalFilename, std::string &serverFilename, std::string &errorMsg) {
    int authUserId = 0;
    std::string authUsername;
    bool isAuth = _sessions->validate(sessionId, authUserId, authUsername);

    std::string query;
    if (!shareCode.empty()) {
        query = "SELECT f.id, f.filename, f.original_filename, f.user_id, "
                "       fs.share_type, fs.shared_with_id, fs.extract_code "
                "FROM files f "
                "JOIN file_shares fs ON f.id = fs.file_id "
                "WHERE f.filename = $1 AND fs.share_code = $2 "
                "AND (fs.expire_time IS NULL OR fs.expire_time > NOW())";
    } else {
        if (!isAuth) {
            errorMsg = "请先登录";
            return true;
        }
        query = "SELECT f.id, f.filename, f.original_filename, f.user_id, "
                "       NULL::varchar, NULL::int, NULL::varchar "
                "FROM files f WHERE f.filename = $1";
    }

    ResultSet rows;
    if (!shareCode.empty()) {
        rows = _db->queryParams(query, {filename, shareCode});
    } else {
        rows = _db->queryParams(query, {filename});
    }

    if (rows.empty()) {
        errorMsg = "文件不存在";
        return true;
    }

    auto &row = rows[0];
    int fileOwnerId = std::stoi(row["user_id"]);
    serverFilename = row["filename"];
    originalFilename = row["original_filename"];
    std::string shareType = shareCode.empty() ? "" : row["share_type"];
    int sharedWithId = 0;
    std::string dbExtractCode;

    if (!shareCode.empty()) {
        sharedWithId = row["shared_with_id"].empty() ? 0 : std::stoi(row["shared_with_id"]);
        dbExtractCode = row["extract_code"];
    }

    // 权限判断
    bool permitted = true;
    if (isAuth && authUserId == fileOwnerId) {
        permitted = true;
    } else if (!shareCode.empty()) {
        if (shareType == "public") {
            permitted = true;
        } else if (shareType == "protected") {
            if (!extractCode.empty() && extractCode == dbExtractCode) {
                permitted = true;
            }
        } else if (shareType == "user" && isAuth && authUserId == sharedWithId) {
            permitted = true;
        }
    } else {
        permitted = true;
    }

    if (!permitted) {
        if (shareType == "protected" && (extractCode.empty() || extractCode != dbExtractCode)) {
            errorMsg = "需要正确的提取码";
        } else {
            errorMsg = "您没有权限访问此文件";
        }
        return true;
    }

    userIdOut = fileOwnerId;
    return true;
}
} // namespace handler
