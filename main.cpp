#include "src/Application.h"
#include <InetAddress.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog-inl.h>

int main(int, char **) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("[ CloudNest ] 服务启动 ...");

    auto server = std::make_shared<Application>(InetAddress{9999, "127.0.0.1"}, "HttpServer");
    server->start();
}
