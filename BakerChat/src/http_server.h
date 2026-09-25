#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>

// /chat 处理回调：收到请求体 JSON，返回响应体 JSON
using ChatHandler = std::function<std::string(const std::string& request_body)>;

class HttpServer {
public:
    HttpServer(int port, const std::string& web_root);
    ~HttpServer();

    void set_chat_handler(ChatHandler h) { chat_handler_ = std::move(h); }

    bool start();
    void stop();

private:
    void accept_loop();
    void handle_client(unsigned long long fd);

    int port_;
    std::string web_root_;
    ChatHandler chat_handler_;
    unsigned long long listen_fd_ = ~0ull;
    std::atomic<bool> running_{false};
};
