#include "FileDownContext.h"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace handler {

namespace fs = std::filesystem;

FileDownContext::FileDownContext(std::string const &filepath, std::string const originalFilename)
    : _filepath(filepath)
    , _originalFilename(originalFilename)
    , _fileSize(0)
    , _currentPos(0)
    , _complete(false) {
    _fileSize = fs::file_size(filepath);
    _file.open(filepath, std::ios::binary | std::ios::in);
    if (!_file.is_open()) {
        spdlog::error("Failed to open file: {}", filepath);
        throw std::runtime_error("无法打开文件: " + filepath);
    }
    spdlog::debug("Openning file for download: {} with size: {}", filepath, _fileSize);
}

FileDownContext::~FileDownContext() {
    if (_file.is_open()) {
        _file.close();
    }
}

void FileDownContext::seekTo(long pos) {
    _file.seekg(pos, std::ios::beg);
    _currentPos = static_cast<uintmax_t>(pos);
    _complete = false;
}

bool FileDownContext::readNextChunk(std::string &chunk, uintmax_t chunkSize) {
    if (_complete || !_file.is_open()) {
        spdlog::debug("读取下一块文件失败: 文件已经读取完成或未能打开");
        return false;
    }
    uintmax_t remaining = _fileSize - _currentPos;
    if (remaining == 0) {
        _complete = true;
        return true;
    }

    uintmax_t toRead = std::min(chunkSize, remaining);
    std::vector<char> buffer(toRead);
    _file.read(buffer.data(), static_cast<int>(toRead));
    chunk.assign(buffer.data(), toRead);
    _currentPos += toRead;
    if (_currentPos >= _fileSize) {
        _complete = true;
        spdlog::debug("文件: {} 已经读取完毕,共计: {} KB", _originalFilename, _fileSize / 1024);
    }
    return true;
}

bool FileDownContext::isComplete() const {
    return _complete;
}

uintmax_t FileDownContext::getFileSize() const {
    return _fileSize;
}

std::string const &FileDownContext::getOriginalFilename() const {
    return _originalFilename;
}
} // namespace handler
