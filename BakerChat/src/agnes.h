#pragma once
#include <string>

// 调用 Agnes，返回 reply 内容。err_out 为空表示成功。
std::string call_agnes(const std::string& messages_json,
    const std::string& api_key,
    const std::string& region,
    const std::string& model,
    std::string& err_out);

// 测试连接：返回 true 表示 Key 有效
bool test_agnes_key(const std::string& api_key,
    const std::string& region,
    std::string& err_out);