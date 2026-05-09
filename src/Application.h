
#pragma once

#include "handler/HttpUploadHandler.h"
#include <EventLoop.h>
#include <HttpServer.h>
#include <InetAddress.h>
#include <thread>

class Application {
public:
    /**
     * @brief Construct a new Application object,
     by default,port is 9999 and host is local
     *
     * @param address
     * @param name
     */
    Application(
        InetAddress address = InetAddress(9999, "127.0.0.1"), std::string const &name = "Server");
    void start(unsigned int threadNul = std::thread::hardware_concurrency());

private:
    EventLoop loop;
    HttpServer server;
    std::shared_ptr<handler::HttpUploadHandler> _handler;
};
