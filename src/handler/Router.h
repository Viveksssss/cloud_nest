// handler/Router.h
#pragma once

#include <functional>
#include <HttpRequest.h>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

// 前向声明（你的项目中的类型）
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
class HttpResponse;

namespace handler {

/**
 * @brief HTTP 路由处理器
 *
 * 支持：
 * - 精确路径匹配: /login, /upload
 * - 正则参数匹配: /download/([^/]+) → filename
 *
 * 使用示例：
 * @code
 *   Router router;
 *   router.add("/login", HttpRequest::kPost,
 *       [](auto& conn, auto& req, auto* resp) -> bool {
 *           // 处理登录
 *           return true;
 *       });
 *
 *   router.add("/download/([^/]+)", HttpRequest::kGet,
 *       [](auto& conn, auto& req, auto* resp) -> bool {
 *           std::string filename = req.getPathParam("filename");
 *           // 下载文件
 *           return true;
 *       }, {"filename"});
 * @endcode
 */
class Router {
public:
    /// 路由处理函数签名
    /// @return true 表示同步处理完成，false 表示需要等待更多数据（如分块上传）
    using Handler = std::function<bool(TcpConnectionPtr const &, HttpRequest &, HttpResponse *)>;

    /**
     * @brief 添加精确路径匹配路由
     *
     * @param path 精确路径，如 "/login"
     * @param method HTTP 方法
     * @param handler 处理函数
     */
    void add(std::string const &path, HttpRequest::Method method, Handler handler);

    /**
     * @brief 添加带参数的正则路由
     *
     * @param pattern 正则表达式模式，如 "/download/([^/]+)"
     * @param method HTTP 方法
     * @param handler 处理函数
     * @param paramNames 参数名列表，按正则捕获组顺序，如 {"filename"}
     */
    void add(std::string const &pattern, HttpRequest::Method method, Handler handler,
        std::vector<std::string> const &paramNames);

    /**
     * @brief 匹配路由
     *
     * @param path 请求路径
     * @param method HTTP 方法
     * @param[out] pathParams 匹配成功后提取的路径参数（如 {"filename": "abc.txt"}）
     * @return 匹配到的 Handler，未匹配则返回空 Handler（nullptr）
     */
    Handler match(std::string const &path, HttpRequest::Method method,
        std::unordered_map<std::string, std::string> &pathParams) const;

private:
    /// 路由条目
    struct RouteEntry {
        std::regex pattern;
        std::vector<std::string> paramNames; // 参数名列表
        Handler handler;
        HttpRequest::Method method;

        RouteEntry(std::string const &patternStr, std::vector<std::string> const &names, Handler h,
            HttpRequest::Method m)
            : pattern(patternStr)
            , paramNames(names)
            , handler(std::move(h))
            , method(m) {
        }
    };

    /// 所有路由条目
    std::vector<RouteEntry> _routes;

    /// 转义正则特殊字符（用于精确匹配路径）
    static std::string escapeRegex(std::string const &str);
};

} // namespace handler
