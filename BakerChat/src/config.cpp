#include "config.h"
#include "utils.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "bcrypt.lib")

static const std::string PASSPHRASE = "Agnes-AI-Workbench#LocalConfig#2026";
static const std::string SALT = "agnes/ai/chat/workbench/v1";

// ---------- SHA256 ----------
static std::vector<unsigned char> sha256(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> hash(32);
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
    BCryptFinishHash(hHash, hash.data(), 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return hash;
}

// ---------- keystream（SHA256 派生） ----------
static std::vector<unsigned char> make_keystream(size_t len) {
    std::vector<unsigned char> seed_in(PASSPHRASE.begin(), PASSPHRASE.end());
    seed_in.insert(seed_in.end(), SALT.begin(), SALT.end());
    auto seed = sha256(seed_in);

    std::vector<unsigned char> out;
    out.reserve(len + 32);
    unsigned int counter = 0;
    while (out.size() < len) {
        std::vector<unsigned char> block = seed;
        unsigned char cb[4] = {
            (unsigned char)(counter >> 24),
            (unsigned char)(counter >> 16),
            (unsigned char)(counter >> 8),
            (unsigned char)counter
        };
        block.insert(block.end(), cb, cb + 4);
        auto h = sha256(block);
        out.insert(out.end(), h.begin(), h.end());
        counter++;
    }
    out.resize(len);
    return out;
}

static std::vector<unsigned char> xor_bytes(const std::vector<unsigned char>& data) {
    auto ks = make_keystream(data.size());
    std::vector<unsigned char> out(data.size());
    for (size_t i = 0; i < data.size(); i++) out[i] = data[i] ^ ks[i];
    return out;
}

// ---------- Base64 ----------
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const std::vector<unsigned char>& data) {
    std::string out;
    for (size_t i = 0; i < data.size(); i += 3) {
        unsigned int v = data[i] << 16;
        if (i + 1 < data.size()) v |= data[i + 1] << 8;
        if (i + 2 < data.size()) v |= data[i + 2];
        out += B64[(v >> 18) & 63];
        out += B64[(v >> 12) & 63];
        out += (i + 1 < data.size()) ? B64[(v >> 6) & 63] : '=';
        out += (i + 2 < data.size()) ? B64[v & 63] : '=';
    }
    return out;
}

static std::vector<unsigned char> b64_decode(const std::string& s) {
    std::vector<int> tbl(256, -1);
    for (int i = 0; i < 64; i++) tbl[(unsigned char)B64[i]] = i;
    std::vector<unsigned char> out;
    int val = 0, bits = -8;
    for (char c : s) {
        if (tbl[(unsigned char)c] == -1) continue;
        val = (val << 6) | tbl[(unsigned char)c];
        bits += 6;
        if (bits >= 0) {
            out.push_back((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

// ---------- 极简 JSON ----------
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

// ============ 公共 API ============

std::string get_config_path() {
    std::string base = get_exe_dir();
    std::string dir = base + "\\config";

    // 用宽字符 API 创建目录（支持中文路径）
    int wlen = MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wdir(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, dir.c_str(), -1, &wdir[0], wlen);
        CreateDirectoryW(wdir.c_str(), NULL);
    }

    return dir + "\\settings.dat";
}

std::string encrypt_to_base64(const std::string& plain) {
    std::vector<unsigned char> data(plain.begin(), plain.end());
    auto enc = xor_bytes(data);
    return b64_encode(enc);
}

std::string decrypt_from_base64(const std::string& b64) {
    auto blob = b64_decode(b64);
    auto dec = xor_bytes(blob);
    return std::string(dec.begin(), dec.end());
}

bool load_config(Config& out) {
    std::string path = get_config_path();
    std::string blob = read_file(path);
    if (blob.empty()) {
        std::cout << "[Config] 配置文件不存在或为空: " << path << std::endl;
        return false;
    }

    blob.erase(std::remove(blob.begin(), blob.end(), '\n'), blob.end());
    blob.erase(std::remove(blob.begin(), blob.end(), '\r'), blob.end());

    std::string json = decrypt_from_base64(blob);
    if (json.empty()) {
        std::cout << "[Config] 解密失败" << std::endl;
        return false;
    }

    out.api_key = json_get_str(json, "api_key");
    out.region = json_get_str(json, "region");
    out.model = json_get_str(json, "model");
    if (out.model.empty())  out.model = "agnes-2.5-flash";
    if (out.region.empty()) out.region = "cn";

    return !out.api_key.empty();
}

bool save_config(const Config& c) {
    std::string path = get_config_path();
    std::cout << "[Config] 保存到: " << path << std::endl;

    std::ostringstream ss;
    ss << "{\"v\":1,"
        << "\"api_key\":\"" << json_escape(c.api_key) << "\","
        << "\"region\":\"" << json_escape(c.region) << "\","
        << "\"model\":\"" << json_escape(c.model) << "\"}";
    std::string b64 = encrypt_to_base64(ss.str());
    std::cout << "[Config] 加密后长度: " << b64.size() << std::endl;

    bool ok = write_file(path, b64);
    std::cout << "[Config] 写入结果: " << (ok ? "成功" : "失败") << std::endl;
    return ok;
}