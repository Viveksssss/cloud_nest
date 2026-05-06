#pragma once

#include "StringUtils.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <unordered_map>

namespace utils {

namespace fs = std::filesystem;

/**
 * @brief 获取文件的扩展名（不含点号，已转小写）
 * @param filename 文件名
 * @return 小写扩展名，"unknown" 表示无扩展名
 */
static inline std::string getExtension(std::string const &filename) {
    size_t dotPos = filename.find_first_of('.');
    if (dotPos == std::string::npos || dotPos == filename.length() - 1) {
        return "unknown";
    }
    return toLower(filename.substr(dotPos + 1));
}

/**
 * @brief 根据文件扩展名判断文件类型
 *
 * @param filename 文件名（可含完整路径，只取扩展名）
 * @return 文件类型字符串:
 *         "image", "video", "audio", "pdf",
 *         "word", "excel", "powerpoint",
 *         "text", "archive", "code",
 *         "other", "unknown"
 */
static inline std::string getFileType(std::string const &filename) {
    std::string ext = getExtension(filename);
    static std::unordered_map<std::string, std::string> const typeMap = {
        // 图片
        {"jpg", "image"},
        {"jpeg", "image"},
        {"png", "image"},
        {"gif", "image"},
        {"bmp", "image"},
        {"svg", "image"},
        {"webp", "image"},
        {"ico", "image"},
        // 视频
        {"mp4", "video"},
        {"avi", "video"},
        {"mov", "video"},
        {"wmv", "video"},
        {"flv", "video"},
        {"mkv", "video"},
        {"webm", "video"},
        // 音频
        {"mp3", "audio"},
        {"wav", "audio"},
        {"flac", "audio"},
        {"aac", "audio"},
        {"ogg", "audio"},

        // 文档
        {"pdf", "pdf"},
        {"doc", "word"},
        {"docx", "word"},
        {"xls", "excel"},
        {"xlsx", "excel"},
        {"ppt", "powerpoint"},
        {"pptx", "powerpoint"},
        {"csv", "text"},
        {"txt", "text"},
        {"md", "text"},
        {"log", "text"},
        {"cpp", "text"},
        {"h", "text"},
        {"py", "text"},
        {"js", "text"},
        {"java", "text"},
        {"html", "text"},
        {"css", "text"},
        {"json", "text"},
        {"xml", "text"},

        // 压缩包
        {"zip", "archive"},
        {"rar", "archive"},
        {"7z", "archive"},
        {"tar", "archive"},
        {"gz", "archive"},
    };

    auto it = typeMap.find(ext);
    return it != typeMap.end() ? it->second : "unknown";
}

/**
 * @brief 生成唯一文件名
 *
 * 格式: {prefix}_{timestamp}_{random}.{ext}
 * 示例: "upload_1701234567890_5432.txt"
 *
 * @param prefix 前缀（如 "upload", "avatar"）
 * @param extension 扩展名（可选，不含点号）
 * @return 唯一文件名
 */
static inline std::string generateUniqueFilename(
    std::string const &prefix, std::string const &extension = "") {
    auto now = std::chrono::system_clock::now();
    auto timestamp
        = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(1000, 9999);
    int random = dis(gen);

    std::string filename = prefix + '_' + std::to_string(timestamp) + "_" + std::to_string(random);

    if (!extension.empty()) {
        filename += '.' + extension;
    }
    return filename;
}

/**
 * @brief 生成唯一文件名（基于原始文件名保留扩展名）
 *
 * @param prefix 前缀
 * @param originalFilename 原始文件名（提取扩展名用）
 * @return 唯一文件名
 */
static inline std::string generateUniqueFilenameFrom(
    std::string const &prefix, std::string const &originalFilename) {
    std::string ext = getExtension(originalFilename);
    if (ext == "unknown") {
        return generateUniqueFilename(prefix);
    }
    return generateUniqueFilename(prefix, ext);
}

/**
 * @brief 确保目录存在，不存在则递归创建
 * @param path 目录路径
 * @return true=成功或已存在, false=创建失败
 */
static inline bool ensureDirectory(std::string const &path) {
    // try {
    //     if (fs::exists(path)) {
    //         return fs::is_directory(path);
    //     }
    //     return fs::create_directory(path);
    // } catch (std::exception const &e) {
    //     return false;
    // }
    std::error_code err;

    if (fs::exists(path, err)) {
        if (!err) {
            return fs::is_directory(path);
        }
        return false;
    }
    if (err) {
        return false;                       // 不存在且发生了错误，失败
    }
    return fs::create_directory(path, err); // 不存在且无错误，尝试创建
}

/**
 * @brief 格式化文件大小为人类可读字符串
 * @param bytes 字节数
 * @return "1.23 MB" 格式
 */
static inline std::string formatFileSize(uintmax_t bytes) {
    char const *units[] = {"B", "KB", "MB", "GB", "TB"};
    size_t unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && ((unitIndex + 1) < sizeof(units) / sizeof(units[0]))) {
        size /= 1024.0;
        ++unitIndex;
    }

    char buffer[32];
    if (unitIndex == 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", size, units[0]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
    }
    return buffer;
}

/**
 * @brief 安全的路径拼接
 * @param base 基础路径
 * @param relative 相对路径
 * @return 拼接后的路径
 */
static inline std::string joinPath(std::string const &base, std::string const &relative) {
    if (base.empty()) {
        return relative;
    }

    if (relative.empty()) {
        return base;
    }
    if (base.back() == '/') {
        return relative.front() == '/' ? base + relative.substr(1) : base + relative;
    } else {
        return relative.front() == '/' ? base + relative : base + "/" + relative;
    }
}
} // namespace utils
