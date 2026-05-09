#include "Application.h"
#include "db/PgDatabase.h"
#include "handler/HttpUploadHandler.h"
#include <db/SqlPool.h>
#include <HttpServer.h>
#include <InetAddress.h>
#include <memory>

Application::Application(InetAddress address, std::string const &name)
    : server(&loop, address, name) {
    auto sqlPool = std::make_shared<SqlPool>("127.0.0.1", "file_manager", "vivek", "38324");
    auto db = std::make_shared<PgDatabase>(sqlPool);

    _handler = std::make_shared<handler::HttpUploadHandler>(db, "/home/vivek/tmp/uploads");

    server.setConnectionCallback(
        [this](TcpConnectionPtr const &conn) { _handler->onConnection(conn); });

    server.setHttpCallback(
        [this](TcpConnectionPtr const &conn, HttpRequest &req, HttpResponse *resp) {
            return _handler->onRequest(conn, req, resp);
        });

    // server.setThreadNum(0); // 在 IO 线程中处理
}

void Application::start(unsigned int threadNum) {
    server.setThreadNum(static_cast<int>(threadNum));
    server.start();
    loop.loop();
}
