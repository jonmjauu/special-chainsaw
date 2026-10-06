#include "websocket_server.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

WebSocketServer::WebSocketServer(unsigned port) {
    server_ = soup_server_new(nullptr, nullptr);
    soup_server_add_websocket_handler(server_, "/state", nullptr, nullptr,
                                      on_connected, this, nullptr);
    GError *error = nullptr;
    if (!soup_server_listen_local(server_, port,
                                  static_cast<SoupServerListenOptions>(0), &error)) {
        const std::string message = std::string("WebSocket listener: ") +
            (error ? error->message : "unknown error");
        if (error) g_error_free(error);
        g_object_unref(server_);
        server_ = nullptr;
        throw std::runtime_error(message);
    }
    std::cerr << "Tracking WebSocket: ws://127.0.0.1:" << port << "/state\n";
}

WebSocketServer::~WebSocketServer() {
    for (auto *client : clients_) {
        g_signal_handlers_disconnect_by_data(client, this);
        soup_websocket_connection_close(client, 1000, nullptr);
        g_object_unref(client);
    }
    if (server_) g_object_unref(server_);
}

void WebSocketServer::on_connected(SoupServer *, SoupServerMessage *, const char *,
                                   SoupWebsocketConnection *connection, gpointer data) {
    auto &self = *static_cast<WebSocketServer *>(data);
    g_object_ref(connection);
    self.clients_.push_back(connection);
    g_signal_connect(connection, "closed", G_CALLBACK(on_closed), &self);
    std::cerr << "Tracking WebSocket: client connected\n";
}

void WebSocketServer::on_closed(SoupWebsocketConnection *connection, gpointer data) {
    auto &self = *static_cast<WebSocketServer *>(data);
    auto it = std::find(self.clients_.begin(), self.clients_.end(), connection);
    if (it == self.clients_.end()) return;
    self.clients_.erase(it);
    g_signal_handlers_disconnect_by_data(connection, &self);
    g_object_unref(connection);
    std::cerr << "Tracking WebSocket: client disconnected\n";
}

void WebSocketServer::broadcast(const std::string &json) {
    // Hold temporary references in case a send synchronously closes a client.
    const auto targets = clients_;
    for (auto *client : targets) g_object_ref(client);
    for (auto *client : targets) {
        if (soup_websocket_connection_get_state(client) == SOUP_WEBSOCKET_STATE_OPEN)
            soup_websocket_connection_send_text(client, json.c_str());
        g_object_unref(client);
    }
}
