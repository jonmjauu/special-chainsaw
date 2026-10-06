#pragma once

#include <libsoup/soup.h>

#include <string>
#include <vector>

class WebSocketServer {
public:
    explicit WebSocketServer(unsigned port);
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer &) = delete;
    WebSocketServer &operator=(const WebSocketServer &) = delete;

    bool has_clients() const { return !clients_.empty(); }
    void broadcast(const std::string &json);

private:
    static void on_connected(SoupServer *, SoupServerMessage *, const char *,
                             SoupWebsocketConnection *connection, gpointer data);
    static void on_closed(SoupWebsocketConnection *connection, gpointer data);

    SoupServer *server_ = nullptr;
    std::vector<SoupWebsocketConnection *> clients_;
};
