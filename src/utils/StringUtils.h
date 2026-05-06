#pragma once
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <ios>
#include <openssl/evp.h>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace utils {
static inline std::string sha256(std::string const &input) {
    unsigned char hash[32];
    size_t hash_len = 0;

    if (EVP_Q_digest(NULL, "SHA256", NULL, input.data(), input.size(), hash, &hash_len) != 1) {
        throw std::runtime_error("EVP_Q_digest failed");
    }

    std::stringstream ss;
    for (size_t i = 0; i < hash_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

static inline std::string sha256_stream(std::string const &filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    // 1.初始化ctx上下文
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }
    // 2, 初始化操作
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }
    char buf[4096];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        // 3. 逐块更新哈希数据
        if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount())) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestUpdate failed");
        }
    }
    file.close();

    unsigned char hash[32];
    unsigned int hash_len = 0;
    // 4.完成计算
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_MD_CTX_free(ctx);
    }

    // 5. 释放 EVP 上下文
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

static inline std::string urlDecode(std::string const &encode) {
    std::string result;
    size_t len = encode.length();
    result.reserve(len);

    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };

    // // 1. 较慢
    // for (size_t i = 0; i < len; ++i) {
    //     if (encode[i] == '%' && i + 2 < len) {
    //
    //         int value;
    //         std::istringstream is(encode.substr(i + 1, 2));
    //         if (is >> std::hex >> value) {
    //             result += static_cast<char>(value);
    //             i += 2;
    //         } else {
    //             result += '%';
    //         }

    //     } else if (encode[i] == '+') {
    //         result += ' ';
    //     } else {
    //         result += encode[i];
    //     }
    // }

    // 2.改进查表版,快
    for (size_t i = 0; i < encode.size(); ++i) {
        char ch = encode[i];
        if (ch == '%' && i + 2 < len) {
            int high = hexVal(encode[i + 1]);
            int low = hexVal(encode[i + 2]);
            if (high != -1 && low != -1) {
                result += static_cast<char>((high << 4) | low);
                i += 2;
                continue;
            }
        }
        result += (ch == '+') ? ' ' : ch;
    }

    // // 3. c++17 from_chars
    // for (size_t i = 0; i < encode.size(); ++i) {
    //     char ch = encode[i];
    //     if (ch == '%' && i + 2 < len) {
    //         int value;
    //         auto [ptr, ec] = std::from_chars(encode.data() + i + 1, encode.data() + 3, value,
    //         16); if (ec == std::errc()) {
    //             result += static_cast<char>(value);
    //             i += 2;
    //         } else {
    //             result += '%';
    //         }
    //     }
    //     result += (ch == '+') ? ' ' : ch;
    // }
    return result;
}

static inline std::string urlEncode(std::string const &raw) {
    // // 1.流,较慢
    // std::ostringstream encoded;
    // encoded.fill('0');
    // encoded << std::hex;
    // for (char const c: raw) {
    //     // 字母、数字、以及 - _ . ~ 不需要编码
    //     if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
    //         encoded << c;
    //     } else if (c == ' ') {
    //         encoded << '+';
    //     } else {
    //         encoded << '%' << std::setw(2) << static_cast<int>(c);
    //     }
    // }
    // return encoded.str();

    // // 2.手动拼接,转换
    // std::string encoded;
    // size_t len = raw.size();
    // encoded.reserve(len * 2);

    // auto toHex = [](char const c) -> char {
    //     return (c < 10) ? ('0' + c) : ('A' + (c - 10));
    // };
    // for (char const c: raw) {
    //     if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
    //         encoded.push_back(c);
    //     } else if (c == ' ') {
    //         encoded.push_back('+');
    //     } else {
    //         encoded.push_back('%');
    //         encoded.push_back(toHex(c >> 4));
    //         encoded.push_back(c & 0x0F);
    //     }
    // }
    // 3. 查表
    static char const hexDigits[] = "0123456789ABCDEF";
    std::string encoded;
    size_t len = raw.size();
    encoded.reserve(len * 2);

    for (char const c: raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back(c);
        } else if (c == ' ') {
            encoded.push_back('+');
        } else {
            encoded.push_back('%');
            encoded.push_back(hexDigits[(c >> 4) & 0x0F]);
            encoded.push_back(c & 0x0F);
        }
    }
    return encoded;
}

static inline std::string escapeRegex(std::string const &str) {
    static std::string const specialChars = R"(.\+*?^$()[]{}|)";
    std::string result;
    for (char c: str) {                                  // 遍历输入字符串的每个字符
        if (specialChars.find(c) != std::string::npos) { // 如果是特殊字符
            result += '\\';                              // 先添加一个反斜杠进行转义
        }
        result += c;                                     // 再添加原字符
    }
    return result;
}

static inline std::vector<std::string> split(std::string const &str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(str);
    while (std::getline(iss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

static inline std::string trim(std::string const &str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        ++start;
    }
    auto end = str.end();
    do {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

static inline std::string toLower(std::string const &str) {
    return str | std::views::transform(::tolower) | std::ranges::to<std::string>();
}

static inline std::string toUpper(std::string const &str) {
    return str | std::views::transform(::toupper) | std::ranges::to<std::string>();
}

static inline bool startsWith(std::string const &str, std::string const &prefix) {
    // return str.starts_with(prefix);
    if (str.length() < prefix.length()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), str.begin());
}

static inline bool endsWith(std::string const &str, std::string const &suffix) {
    // return str.ends_with(suffix);
    if (str.length() < suffix.length()) {
        return false;
    }
    return std::equal(
        suffix.begin(), suffix.end(), str.begin() + static_cast<long>(str.size() - suffix.size()));
}

} // namespace utils
