#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace handler {
class FileDownContext {
public:
    FileDownContext(std::string const &filepath, std::string const originalFilename);
    ~FileDownContext();

    void seekTo(long pos);
    bool readNextChunk(std::string &chunk, uintmax_t chunkSize = 1024 * 1024); // 默认1MB
    bool isComplete() const;
    uintmax_t getFileSize() const;
    std::string const &getOriginalFilename() const;

private:
    std::string _filepath;
    std::string _originalFilename;
    std::ifstream _file;
    uintmax_t _fileSize = 0;
    uintmax_t _currentPos = 0;
    bool _complete = false;
};

} // namespace handler
