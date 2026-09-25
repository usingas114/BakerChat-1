#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

// 获取 exe 所在目录（返回 UTF-8 编码）
inline std::string get_exe_dir() {
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buf, MAX_PATH);

    std::wstring wpath(buf);
    auto pos = wpath.find_last_of(L"\\/");
    std::wstring wdir = (pos == std::wstring::npos) ? L"." : wpath.substr(0, pos);

    int len = WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return ".";
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
}

// 读文件到 string（支持中文路径）
inline std::string read_file(const std::string& path) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return "";
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);

    std::ifstream f(wpath.c_str(), std::ios::binary);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 写文件（支持中文路径 + 自动创建父目录）
inline bool write_file(const std::string& path, const std::string& data) {
    // ⭐ 先递归创建父目录
    auto pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        std::string dir = path.substr(0, pos);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wdir(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, &wdir[0], wlen);
            // 逐级创建
            for (size_t i = 2; i < wdir.size(); i++) {
                if (wdir[i] == L'\\' || wdir[i] == L'/') {
                    std::wstring sub = wdir.substr(0, i);
                    CreateDirectoryW(sub.c_str(), NULL);
                }
            }
            CreateDirectoryW(wdir.c_str(), NULL);
        }
    }

    // 写入文件
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);

    std::ofstream f(wpath.c_str(), std::ios::binary);
    if (!f) return false;
    f.write(data.data(), data.size());
    return f.good();
}

// MIME 类型（按扩展名）
inline std::string mime_type(const std::string& path) {
    auto ends = [&](const char* s) {
        size_t n = strlen(s);
        return path.size() >= n &&
            path.compare(path.size() - n, n, s) == 0;
        };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js"))   return "application/javascript; charset=utf-8";
    if (ends(".css"))  return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".webp")) return "image/webp";
    if (ends(".png"))  return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".svg"))  return "image/svg+xml";
    if (ends(".woff2")) return "font/woff2";
    if (ends(".woff"))  return "font/woff";
    if (ends(".ico"))   return "image/x-icon";
    return "application/octet-stream";
}