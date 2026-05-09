# cloud_nest

高性能 C++ 云文件存储与分享服务，基于 muduo 网络库构建。

## 依赖

| 依赖 | 用途 |
|------|------|
| **muduo** | 网络框架（HTTP 服务、TCP 连接、事件循环） |
| **nlohmann_json** | JSON 解析与序列化 |
| **spdlog** | 日志系统 |
| **OpenSSL** | 密码学（密码哈希） |
| **libpqxx** | PostgreSQL C++ 客户端 |
| **Boost** | 通用工具库 |
| **CMake** >= 3.10 | 构建系统 |
| **PostgreSQL** | 数据库 |

## C++ 标准

C++17（兼容 C++20，`CMAKE_CXX_STANDARD` 设为 20）。

## 安装方式

### 1. 安装系统依赖

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libboost-all-dev \
    libssl-dev libpq-dev libpqxx-dev libspdlog-dev \
    nlohmann-json3-dev

# muduo 需要从源码编译安装
git clone https://github.com/Viveksssss/muduo.git
cd muduo
./auto_build.sh
```

### 2. 初始化数据库

```bash
sudo -u postgres psql -f psql.txt
```

### 3. 编译项目

```bash
https://github.com/Viveksssss/cloud_nest.git
cd cloud_nest
mkdir build && cd build
cmake ..
make -j8
```

### 4. 修改配置

编辑 `src/Application.cc`，修改数据库连接参数和上传目录：

```cpp
// 数据库连接: host, dbname, user, password
auto sqlPool = std::make_shared<SqlPool>("127.0.0.1", "file_manager", "vivek", "38324");
// 上传目录
_handler = std::make_shared<handler::HttpUploadHandler>(db, "/home/vivek/tmp/uploads");
```

### 5. 启动服务

```bash
./bin/cloud_nest
# 默认监听 0.0.0.0:9999
```

## 使用方式示例

### 用户注册

```bash
curl -X POST http://localhost:9999/register \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"123456","email":"alice@example.com"}'
```

### 用户登录

```bash
# 登录并获取 sessionId
curl -X POST http://localhost:9999/login \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"123456"}'

# 响应: {"code":0,"message":"登录成功","sessionId":"<32位会话ID>"}
```

### 上传文件

```bash
curl -X POST http://localhost:9999/upload \
  -H "X-Session-ID: <sessionId>" \
  -H "X-File-Name: report.pdf" \
  --data-binary @report.pdf
```

### 列出文件

```bash
# 我的文件
curl http://localhost:9999/files?type=my \
  -H "X-Session-ID: <sessionId>"

# 共享给我的
curl http://localhost:9999/files?type=shared \
  -H "X-Session-ID: <sessionId>"

# 全部 + 关键词搜索
curl "http://localhost:9999/files?type=all&keyword=report" \
  -H "X-Session-ID: <sessionId>"
```

### 下载文件

```bash
curl -O -H "X-Session-ID: <sessionId>" \
  http://localhost:9999/download/<serverFilename>
```

### 删除文件

```bash
curl -X DELETE http://localhost:9999/delete/<serverFilename> \
  -H "X-Session-ID: <sessionId>"
```

### 创建分享

```bash
# 公开分享
curl -X POST http://localhost:9999/share \
  -H "Content-Type: application/json" \
  -H "X-Session-ID: <sessionId>" \
  -d '{"fileId":1,"shareType":"public"}'

# 提取码分享
curl -X POST http://localhost:9999/share \
  -H "Content-Type: application/json" \
  -H "X-Session-ID: <sessionId>" \
  -d '{"fileId":1,"shareType":"protected","expireTime":24}'

# 指定用户分享
curl -X POST http://localhost:9999/share \
  -H "Content-Type: application/json" \
  -H "X-Session-ID: <sessionId>" \
  -d '{"fileId":1,"shareType":"user","sharedWithId":2}'

# 取消分享（设为私有）
curl -X POST http://localhost:9999/share \
  -H "Content-Type: application/json" \
  -H "X-Session-ID: <sessionId>" \
  -d '{"fileId":1,"shareType":"private"}'
```

### 通过分享链接访问

```bash
# 浏览器访问
http://localhost:9999/share/<shareCode>

# API 获取分享信息
curl http://localhost:9999/share/<shareCode> \
  -H "Accept: application/json"

# 受保护的分享需要提取码
curl "http://localhost:9999/share/<shareCode>?code=<extractCode>" \
  -H "Accept: application/json"

# 通过分享链接下载
curl -O "http://localhost:9999/share/download/<filename>?code=<shareCode>"
```

### 搜索用户

```bash
curl "http://localhost:9999/users/search?keyword=bob" \
  -H "X-Session-ID: <sessionId>"
```

## 整体架构

```
main.cpp
└── Application                          # 应用入口，组装所有组件
    ├── HttpServer (muduo)               # HTTP 服务器（事件驱动、多线程）
    └── HttpUploadHandler                # 中央请求处理器
        ├── Router                       # URL 路由匹配（支持精确路径 + 正则参数）
        ├── AuthHandler                  # 用户认证（注册/登录/登出）
        ├── FileHandler                  # 文件操作（上传/下载/列表/删除）
        ├── ShareHandler                 # 分享管理（创建/访问/信息/下载）
        ├── SessionManager               # 会话管理（创建/验证/销毁，30分钟自动续期）
        ├── StaticFileHandler            # 静态资源服务（HTML/ICO）
        ├── SqlPool                      # PostgreSQL 连接池（RAII ConnectionGuard）
        └── PgDatabase                   # 数据库接口实现（参数化查询防注入）
```

### 数据流

```
Client Request → muduo HttpServer → HttpUploadHandler.onRequest()
    → Router.match() → Handler → Database (PgDatabase → SqlPool → PostgreSQL)
    → JSON Response / File Stream
```

### 数据库表结构

| 表 | 说明 |
|----|------|
| `users` | 用户（id, username, password, email） |
| `files` | 文件（id, filename, original_filename, size, type, user_id） |
| `sessions` | 会话（id, session_id, user_id, expire_time） |
| `file_shares` | 分享（id, file_id, share_type, share_code, extract_code, shared_with_id, expire_time） |

## 功能介绍

- **用户系统** — 注册、登录、登出，基于会话的身份认证，密码经 OpenSSL 哈希存储
- **文件管理** — 支持 multipart/form-data 上传、分块下载（HTTP Range）、文件列表（按所有权/分享分类）、关键词搜索、文件删除
- **四种分享模式**
  - `public` — 任何人可访问
  - `protected` — 需要提取码验证
  - `user` — 仅指定用户可访问
  - `private` — 取消分享
- **分享页面** — 独立的 `/share/{code}` 页面，前端自动判断权限并提示
- **用户搜索** — 支持模糊匹配，用于指定用户分享时查找目标用户
- **安全性** — 参数化 SQL 查询防注入、密码哈希存储、会话超时自动过期
- **高性能** — 基于 muduo Reactor 模型，支持多线程 IO 处理，RAII 连接池管理数据库连接

[![peLYBdg.png](https://s41.ax1x.com/2026/05/09/peLYBdg.png)](https://imgchr.com/i/peLYBdg)

[![peLYDoQ.png](https://s41.ax1x.com/2026/05/09/peLYDoQ.png)](https://imgchr.com/i/peLYDoQ)

[![peLYsij.png](https://s41.ax1x.com/2026/05/09/peLYsij.png)](https://imgchr.com/i/peLYsij)

[![peLYyJs.png](https://s41.ax1x.com/2026/05/09/peLYyJs.png)](https://imgchr.com/i/peLYyJs)

[![peLYheU.png](https://s41.ax1x.com/2026/05/09/peLYheU.png)](https://imgchr.com/i/peLYheU)

[![peLY5o4.png](https://s41.ax1x.com/2026/05/09/peLY5o4.png)](https://imgchr.com/i/peLY5o4)
