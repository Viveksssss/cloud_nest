#pragma once
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace handler {

class FileUploadContext {
public:
    enum class State {
        ExpectHaders,
        ExpectContent,
        ExpectBounday,
        Complete
    };

    FileUploadContext(std::string const &filename, std::string const &originalFilename);
    ~FileUploadContext();
    void writeData(char const *data, size_t len);
    void setBoundary(std::string const &boundary);
    void setState(State state);

    State getState() const;
    std::string const &getBoundary() const;
    std::string const &getFilename() const;
    std::string const &getOriginalFilename() const;

    uintmax_t getTotalBytes() const;

private:
    std::string _filepath;
    std::string _originalFilename;
    std::ofstream _file;
    uintmax_t _totalBytes = 0;
    State _state = State::ExpectHaders;
    std::string _boundary; // multipart边界
};

} // namespace handler
