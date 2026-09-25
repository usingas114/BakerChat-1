#include "http_server.h"
#include "utils.h"
#include "config.h"
#include "agnes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

static void log_http(const std::string& s) {
    std::cout << "[HTTP] " << s << std::endl;
}

// ===================== URL 解码 =====================
static std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
                };
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') out += ' ';
        else out += in[i];
    }
    return out;
}

// ===================== JSON 工具 =====================
static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                out += buf;
            }
            else out += c;
        }
    }
    return out;
}

// 从 JSON 里抠字符串字段
static std::string json_get_str(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    auto p = j.find(pat);
    if (p == std::string::npos) {
        pat = "\"" + key + "\": \"";
        p = j.find(pat);
        if (p == std::string::npos) return "";
    }
    p += pat.size();
    std::string out;
    for (size_t i = p; i < j.size(); i++) {
        char c = j[i];
        if (c == '\\' && i + 1 < j.size()) {
            char n = j[i + 1];
            if (n == 'n') { out += '\n'; i++; continue; }
            if (n == 't') { out += '\t'; i++; continue; }
            if (n == 'r') { out += '\r'; i++; continue; }
            if (n == '"') { out += '"'; i++; continue; }
            if (n == '\\') { out += '\\'; i++; continue; }
            out += n; i++; continue;
        }
        if (c == '"') break;
        out += c;
    }
    return out;
}

// ===================== 构造器 =====================
HttpServer::HttpServer(int port, const std::string& web_root)
    : port_(port), web_root_(web_root) {
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_http("WSAStartup 失败");
        return false;
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        log_http("socket() 失败");
        return false;
    }
    listen_fd_ = (unsigned long long)fd;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        log_http("bind 失败：端口 " + std::to_string(port_) + " 可能被占用");
        closesocket(fd);
        listen_fd_ = ~0ull;
        return false;
    }
    if (listen(fd, SOMAXCONN) == SOCKET_ERROR) {
        log_http("listen 失败");
        closesocket(fd);
        listen_fd_ = ~0ull;
        return false;
    }

    running_ = true;
    std::thread([this]() { accept_loop(); }).detach();
    log_http("已启动 http://127.0.0.1:" + std::to_string(port_) + "  (根目录: " + web_root_ + ")");
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ != ~0ull) {
        closesocket((SOCKET)listen_fd_);
        listen_fd_ = ~0ull;
    }
}

void HttpServer::accept_loop() {
    while (running_) {
        sockaddr_in cli{};
        int len = sizeof(cli);
        SOCKET c = accept((SOCKET)listen_fd_, (sockaddr*)&cli, &len);
        if (c == INVALID_SOCKET) {
            if (!running_) break;
            continue;
        }
        std::thread([this, c]() { handle_client((unsigned long long)c); }).detach();
    }
}

// ===================== HTTP 读行 =====================
static std::string read_line(SOCKET s) {
    std::string line;
    char c;
    while (true) {
        int n = recv(s, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

// ===================== /setup 页面 HTML =====================
static const char* SETUP_HTML = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>BakerChat 首次配置</title>
<style>
  *{box-sizing:border-box}
  body{background:#141413;color:#e8e8e8;font-family:"HarmonyOS Sans SC",system-ui,-apple-system,sans-serif;
       display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:24px}
  .box{background:#1c1c1e;padding:40px;border-radius:14px;width:460px;max-width:100%;
       box-shadow:0 8px 40px rgba(0,0,0,.5);border:1px solid #2a2a2c}
  h1{font-size:22px;margin:0 0 8px;color:#f7ff48;letter-spacing:.5px}
  .sub{font-size:13px;color:#888;margin-bottom:28px}
  label{display:block;margin:18px 0 8px;font-size:13px;color:#aaa;letter-spacing:.3px}
  input,select{width:100%;padding:11px 14px;
    background:#0d0d0e;border:1px solid #333;border-radius:8px;color:#eee;font-size:14px;
    font-family:inherit;transition:border-color .15s}
  input:focus,select:focus{outline:none;border-color:#f7ff48}
  button{margin-top:24px;width:100%;padding:13px;background:#f7ff48;border:none;
    border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;color:#111;font-family:inherit;
    transition:background .15s}
  button:hover:not(:disabled){background:#e6ee3c}
  button:disabled{background:#444;cursor:wait;color:#888}
  #msg{margin-top:14px;font-size:13px;min-height:22px;line-height:1.5}
  .ok{color:#4ade80} .err{color:#f87171} .info{color:#fbbf24}
  .hint{margin-top:20px;font-size:12px;color:#666;line-height:1.7;
        padding-top:16px;border-top:1px solid #2a2a2c}
  .hint a{color:#f7ff48;text-decoration:none}
  .hint a:hover{text-decoration:underline}
</style>
</head>
<body>
<div class="box">
  <h1>//BAKER/</h1>
  <div class="sub">首次使用配置 · 本地加密存储</div>

  <label>API 密钥</label>
  <input id="key" type="password" placeholder="sk-..." autocomplete="off" spellcheck="false">

  <label>服务区</label>
  <select id="region">
    <option value="cn">.cn 国区（api.agnes-ai.cn）</option>
    <option value="com">.com 国际区（api.agnes-ai.com）</option>
  </select>

  <label>模型</label>
  <select id="model">
    <option value="agnes-2.5-flash">agnes-2.5-flash（推荐）</option>
    <option value="agnes-2.5-pro">agnes-2.5-pro</option>
    <option value="agnes-3.0-flash">agnes-3.0-flash</option>
    <option value="agnes-2.0-flash">agnes-2.0-flash</option>
  </select>

  <button id="btn" onclick="save()">验证并保存</button>
  <div id="msg"></div>

  <div class="hint">
    密钥将加密保存在 <code>config/settings.dat</code>，不会上传到任何服务器。<br>
    获取密钥：<a href="https://api.agnes-ai.cn/" target="_blank">api.agnes-ai.cn</a>
  </div>
</div>
<script>
async function save(){
  const key = document.getElementById('key').value.trim();
  const region = document.getElementById('region').value;
  const model = document.getElementById('model').value;
  const msg = document.getElementById('msg');
  const btn = document.getElementById('btn');

  if (!key) { msg.className='err'; msg.textContent='❌ 请输入 API 密钥'; return; }

  btn.disabled = true;
  btn.textContent = '正在验证…';
  msg.className = 'info';
  msg.textContent = '正在连接 Agnes 服务器…';

  try {
    const r = await fetch('/setup/save', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({api_key:key, region:region, model:model})
    });
    const j = await r.json();
    if (j.ok) {
      msg.className = 'ok';
      msg.textContent = '✅ 保存成功，正在跳转…';
      setTimeout(() => location.href = '/', 1000);
    } else {
      msg.className = 'err';
      msg.textContent = '❌ ' + (j.error || '验证失败');
      btn.disabled = false;
      btn.textContent = '验证并保存';
    }
  } catch(e) {
    msg.className = 'err';
    msg.textContent = '❌ 网络错误: ' + e.message;
    btn.disabled = false;
    btn.textContent = '验证并保存';
  }
}
</script>
</body>
</html>)HTML";

// ===================== 请求处理 =====================
void HttpServer::handle_client(unsigned long long fd) {
    SOCKET s = (SOCKET)fd;

    // 1) 请求行
    std::string req_line = read_line(s);
    if (req_line.empty()) { closesocket(s); return; }

    std::istringstream iss(req_line);
    std::string method, path, version;
    iss >> method >> path >> version;

    // 2) headers
    std::map<std::string, std::string> headers;
    while (true) {
        std::string line = read_line(s);
        if (line.empty()) break;
        auto p = line.find(':');
        if (p == std::string::npos) continue;
        std::string key = line.substr(0, p);
        std::string val = line.substr(p + 1);
        while (!val.empty() && val[0] == ' ') val.erase(0, 1);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        headers[key] = val;
    }

    // 3) body
    std::string body;
    auto it = headers.find("content-length");
    if (it != headers.end()) {
        int n = std::atoi(it->second.c_str());
        if (n > 0) {
            body.resize(n);
            int got = 0;
            while (got < n) {
                int r = recv(s, &body[got], n - got, 0);
                if (r <= 0) break;
                got += r;
            }
        }
    }

    // 4) 路由
    std::string resp_body;
    std::string content_type;
    int status = 200;

    // ---------- /setup/save (POST) ----------
    if (method == "POST" && path == "/setup/save") {
        content_type = "application/json; charset=utf-8";

        std::string key = json_get_str(body, "api_key");
        std::string region = json_get_str(body, "region");
        std::string model = json_get_str(body, "model");
        if (region.empty()) region = "cn";
        if (model.empty())  model = "agnes-2.5-flash";

        log_http("POST /setup/save  (region=" + region + ", model=" + model + ")");

        if (key.empty()) {
            resp_body = "{\"ok\":false,\"error\":\"密钥为空\"}";
        }
        else {
            std::string err;
            if (test_agnes_key(key, region, err)) {
                Config c;
                c.api_key = key;
                c.region = region;
                c.model = model;
                if (save_config(c)) {
                    resp_body = "{\"ok\":true}";
                    log_http("✅ 配置已保存");
                }
                else {
                    resp_body = "{\"ok\":false,\"error\":\"写入文件失败\"}";
                }
            }
            else {
                resp_body = "{\"ok\":false,\"error\":\"" + json_escape(err) + "\"}";
                log_http("❌ 验证失败: " + err);
            }
        }
    }
    // ---------- /chat (POST) ----------
    else if (method == "POST" && path == "/chat") {
        content_type = "application/json; charset=utf-8";
        if (chat_handler_) {
            try {
                resp_body = chat_handler_(body);
            }
            catch (const std::exception& e) {
                status = 500;
                resp_body = std::string("{\"reply\":\"服务器错误: ")
                    + json_escape(e.what()) + "\",\"mood\":null}";
            }
        }
        else {
            resp_body = "{\"reply\":\"(chat handler 未设置)\",\"mood\":null}";
        }
    }
    // ---------- /setup (GET) ----------
    else if (method == "GET" && path == "/setup") {
        resp_body = SETUP_HTML;
        content_type = "text/html; charset=utf-8";
    }
    // ---------- 静态文件 (GET) ----------
    else if (method == "GET") {
        std::string file_path = path;
        if (file_path == "/" || file_path.empty()) file_path = "/index.html";

        auto q = file_path.find('?');
        if (q != std::string::npos) file_path = file_path.substr(0, q);

        std::string decoded = url_decode(file_path);

        // 防目录穿越
        if (decoded.find("..") != std::string::npos) {
            status = 403;
            resp_body = "Forbidden";
            content_type = "text/plain";
        }
        else {
            std::string full = web_root_ + decoded;
            std::replace(full.begin(), full.end(), '/', '\\');
            resp_body = read_file(full);
            if (resp_body.empty()) {
                status = 404;
                resp_body = "Not Found: " + decoded;
                content_type = "text/plain; charset=utf-8";
            }
            else {
                content_type = mime_type(decoded);
            }
        }
    }
    // ---------- OPTIONS ----------
    else if (method == "OPTIONS") {
        status = 204;
    }
    // ---------- 其他 ----------
    else {
        status = 405;
        resp_body = "Method Not Allowed";
        content_type = "text/plain";
    }

    // 5) 组装响应
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " ";
    switch (status) {
    case 200: resp << "OK"; break;
    case 204: resp << "No Content"; break;
    case 400: resp << "Bad Request"; break;
    case 403: resp << "Forbidden"; break;
    case 404: resp << "Not Found"; break;
    case 405: resp << "Method Not Allowed"; break;
    case 500: resp << "Internal Server Error"; break;
    default:  resp << "OK";
    }
    resp << "\r\n";

    if (!content_type.empty())
        resp << "Content-Type: " << content_type << "\r\n";
    resp << "Content-Length: " << resp_body.size() << "\r\n";
    resp << "Access-Control-Allow-Origin: *\r\n";
    resp << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    resp << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    resp << "Connection: close\r\n\r\n";
    resp << resp_body;

    std::string out = resp.str();
    send(s, out.data(), (int)out.size(), 0);
    closesocket(s);
}