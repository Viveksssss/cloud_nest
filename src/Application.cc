#include "Application.h"
#include <HttpServer.h>
#include <InetAddress.h>

Application::Application(InetAddress address, std::string const &name)
    : server(&loop, address, name) {
}

void Application::start(unsigned int threadNum) {
    server.setThreadNum(static_cast<int>(threadNum));
    server.start();
    loop.loop();
}
