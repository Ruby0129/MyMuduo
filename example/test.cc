#include "HTTPServer.h"

int main()
{
    EventLoop loop;
    InetAddress addr(8000);
    HTTPServer server(&loop, addr, "HTTPServer-01"); // Acceptor non-blocking listenfd create bind
    server.start(); // listen loopthread listenfd => acceptChannel => mainLoop
    loop.loop(); // 启动mainLoop的底层Poller

    return 0;
}