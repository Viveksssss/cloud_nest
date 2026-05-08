#include "FileUploadContext.h"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace handler {

namespace fs = std::filesystem;

FileUploadContext::FileUploadContext(
    std::string const &filename, std::string const &originalFilename)
    : _filepath(filename)
    , _originalFilename(originalFilename)
    , _totalBytes(0)
    , _boundary("")
    , _state(State::ExpectHaders) {
    fs::path p(filename);
    if (p.has_parent_path()) {
        try {
            fs::create_directories(p.parent_path());
        } catch (...) {
            spdlog::warn("创建上传目录失败: {}", p.parent_path().string());
        }
    }
    _file.open(_filepath, std::ios::binary | std::ios::out);
    if (!_file.is_open()) {
        spdlog::error("Failed to create file: {}", _filepath);
        throw std::runtime_error("无法创建文件: " + _filepath);
    }
}

FileUploadContext::~FileUploadContext() {
    if (_file.is_open()) {
        _file.close();
    }
}

void FileUploadContext::writeData(char const *data, size_t len) {
    if (!_file.is_open()) {
        throw std::runtime_error("文件已关闭");
    }
    _file.write(data, static_cast<long>(len));
    _totalBytes += len;
}

void FileUploadContext::setBoundary(std::string const &b) {
    _boundary = b;
}

void FileUploadContext::setState(State state) {
    _state = state;
}

FileUploadContext::State FileUploadContext::getState() const {
    return _state;
}

std::string const &FileUploadContext::getBoundary() const {
    return _boundary;
}

std::string const &FileUploadContext::getFilename() const {
    return _filepath;
}

std::string const &FileUploadContext::getOriginalFilename() const {
    return _originalFilename;
}

uintmax_t FileUploadContext::getTotalBytes() const {
    return _totalBytes;
}
} // namespace handler
