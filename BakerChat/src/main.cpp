#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "http_server.h"
#include "utils.h"
#include "config.h"
#include "agnes.h"

//#pragma comment(lib, "shellapi.lib")

// ---------- JSON 转义 ----------
static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c;
        }
    }
    return out;
}

// ---------- 从 JSON 抠字符串字段 ----------
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

// ---------- 从 JSON 抠出 "history" 数组 ----------
static std::string json_get_history(const std::string& body) {
    auto hpos = body.find("\"history\":");
    if (hpos == std::string::npos) return "";
    auto arr_start = body.find('[', hpos);
    if (arr_start == std::string::npos) return "";
    int depth = 0;
    size_t i = arr_start;
    for (; i < body.size(); i++) {
        if (body[i] == '[') depth++;
        else if (body[i] == ']') {
            depth--;
            if (depth == 0) { i++; break; }
        }
    }
    return body.substr(arr_start, i - arr_start);
}

// ---------- 读取角色提示词 ----------
static std::string load_character_prompt(const std::string& base,
    const std::string& character) {
    std::string system_prompt;

    // 1. prompts\{角色中文名}.md
    if (!character.empty()) {
        std::string char_file = base + "\\prompts\\" + character + ".md";
        std::string cf = read_file(char_file);
        if (!cf.empty()) {
            system_prompt = cf;
            std::cout << "[Prompt] ✅ 加载角色: " << character << ".md ("
                << cf.size() << " 字节)" << std::endl;
        }
        else {
            std::cout << "[Prompt] ⚠️ 未找到 " << character << ".md" << std::endl;
        }
    }

    // 2. 兜底：prompts\_default.txt
    if (system_prompt.empty()) {
        std::string def_file = base + "\\prompts\\_default.txt";
        std::string df = read_file(def_file);
        if (!df.empty()) {
            system_prompt = df;
            std::cout << "[Prompt] 使用默认 _default.txt ("
                << df.size() << " 字节)" << std::endl;
        }
    }

    // 3. 再兜底：内置
    if (system_prompt.empty()) {
        system_prompt = "你是一个专业的助手。请严格遵守：\n1. 回答简洁明了\n2. 用中文回答";
        std::cout << "[Prompt] 使用内置默认提示词" << std::endl;
    }

    // 4. 追加通用规则 _rules.txt
    std::string rules_file = base + "\\prompts\\_rules.txt";
    std::string rf = read_file(rules_file);
    if (!rf.empty()) {
        system_prompt += "\n\n";
        system_prompt += rf;
        std::cout << "[Prompt] 已追加 _rules.txt (" << rf.size() << " 字节)" << std::endl;
    }

    return system_prompt;
}

int main() {
    //system("pacong.exe");
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "===== BakerChat (本地版) =====" << std::endl;

    std::string base = get_exe_dir();
    std::string web_root = base + "\\web";
    std::cout << "[Main] exe 目录: " << base << std::endl;
    std::cout << "[Main] web 目录: " << web_root << std::endl;

    Config startup_cfg;
    bool has_cfg_at_start = load_config(startup_cfg);
    if (has_cfg_at_start) {
        std::cout << "[Main] 已加载配置（区域 " << startup_cfg.region
            << "，模型 " << startup_cfg.model
            << "，Key 长度 " << startup_cfg.api_key.size() << "）" << std::endl;
    }
    else {
        std::cout << "[Main] 未检测到 API Key，将引导用户配置" << std::endl;
    }

    HttpServer server(8000, web_root);

    server.set_chat_handler([&](const std::string& body) -> std::string {
        std::cout << "[Chat] 收到: " << body.substr(0, 200) << std::endl;

        Config cfg;
        bool has_cfg = load_config(cfg);
        if (!has_cfg || cfg.api_key.empty()) {
            return "{\"reply\":\"⚠️ 尚未配置 API Key，请访问 http://127.0.0.1:8000/setup 进行配置。\",\"mood\":null}";
        }

        std::string user_msg = json_get_str(body, "message");
        if (user_msg.empty()) user_msg = json_get_str(body, "content");
        std::string character = json_get_str(body, "character");
        std::cout << "[Chat] 角色: " << (character.empty() ? "(未指定)" : character) << std::endl;

        std::string system_prompt = load_character_prompt(base, character);

        std::string messages = "[";
        messages += "{\"role\":\"system\",\"content\":\"" + json_escape(system_prompt) + "\"}";

        std::string history_arr = json_get_history(body);
        if (!history_arr.empty() && history_arr != "[]" && history_arr.size() > 2) {
            std::string inner = history_arr.substr(1, history_arr.size() - 2);
            while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\n')) inner.erase(0, 1);
            while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\n')) inner.pop_back();
            if (!inner.empty()) {
                messages += ",";
                messages += inner;
            }
        }

        messages += ",{\"role\":\"user\",\"content\":\"" + json_escape(user_msg) + "\"}]";

        std::string err;
        std::string reply = call_agnes(messages, cfg.api_key, cfg.region, cfg.model, err);
        if (!err.empty()) {
            std::cout << "[Chat] ❌ Agnes 错误: " << err << std::endl;
            return "{\"reply\":\"❌ " + json_escape(err) + "\",\"mood\":null}";
        }
        std::cout << "[Chat] ✅ 回复: " << reply.substr(0, 120) << std::endl;
        return "{\"reply\":\"" + json_escape(reply) + "\",\"mood\":null}";
        });

    if (!server.start()) {
        std::cerr << "[Main] HTTP 服务启动失败" << std::endl;
        system("pause");
        return 1;
    }

    Sleep(300);
    const char* url = has_cfg_at_start ? "http://127.0.0.1:8000/" : "http://127.0.0.1:8000/setup";
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    std::cout << "\n浏览器已打开。按 Enter 退出...\n" << std::endl;

    std::cin.get();
    server.stop();
    return 0;
}