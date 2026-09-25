#pragma once
#include <string>

struct Config {
    std::string api_key;
    std::string region;   // "cn" 或 "com"
    std::string model;    // 默认 "agnes-2.5-flash"
};

// 配置路径 = exe目录/config/settings.dat
std::string get_config_path();

// 读配置（解密后返回 true）
bool load_config(Config& out);

// 写配置（加密保存）
bool save_config(const Config& c);

// 加密/解密工具（调试用）
std::string encrypt_to_base64(const std::string& plain);
std::string decrypt_from_base64(const std::string& b64);