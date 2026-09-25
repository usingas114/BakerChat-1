#include "agnes.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

static std::wstring u2w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// 通用 HTTPS POST，返回 true 表示 200 OK，响应体在 response_out
static bool https_post(const std::wstring& host,
    const std::wstring& path,
    const std::string& body,
    const std::wstring& auth,
    std::string& response_out,
    std::string& err_out)
{
    response_out.clear();

    HINTERNET hSession = WinHttpOpen(L"BakerChat/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { err_out = "WinHttpOpen 失败"; return false; }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        err_out = "WinHttpConnect 失败";
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        err_out = "WinHttpOpenRequest 失败";
        return false;
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!auth.empty()) { headers += auth; headers += L"\r\n"; }

    BOOL ok = WinHttpSendRequest(hRequest,
        headers.c_str(), (DWORD)-1L,
        (LPVOID)body.data(), (DWORD)body.size(),
        (DWORD)body.size(), 0);
    if (!ok) {
        err_out = "WinHttpSendRequest 失败 err=" + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        err_out = "WinHttpReceiveResponse 失败";
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);

    // 无论成功失败都读 body
    DWORD avail = 0;
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        WinHttpReadData(hRequest, buf.data(), avail, &read);
        response_out.append(buf.data(), read);
    } while (avail > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (status != 200) {
        err_out = "HTTP " + std::to_string(status);
        if (!response_out.empty() && response_out.size() > 300)
            response_out = response_out.substr(0, 300);
        return false;
    }
    return true;
}

// ---------- 测试 Key ----------
bool test_agnes_key(const std::string& api_key,
    const std::string& region,
    std::string& err_out)
{
    if (api_key.empty()) { err_out = "密钥为空"; return false; }

    std::wstring host = (region == "com") ? L"api.agnes-ai.com" : L"api.agnes-ai.cn";
    std::wstring auth = L"Authorization: Bearer " + u2w(api_key);

    std::string body = "{\"model\":\"agnes-2.5-flash\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}],\"max_tokens\":5,\"stream\":false}";
    std::string resp;
    if (!https_post(host, L"/v1/chat/completions", body, auth, resp, err_out)) {
        return false;
    }
    return true;
}

// ---------- 解析 content（从响应里抠） ----------
static std::string extract_content(const std::string& resp) {
    auto pos = resp.find("\"content\"");
    if (pos == std::string::npos) return "";
    pos = resp.find('"', pos + 9);
    if (pos == std::string::npos) return "";
    pos++;

    std::string out;
    for (size_t i = pos; i < resp.size(); i++) {
        char c = resp[i];
        if (c == '\\' && i + 1 < resp.size()) {
            char n = resp[i + 1];
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

// ---------- 调用 Agnes ----------
std::string call_agnes(const std::string& messages_json,
    const std::string& api_key,
    const std::string& region,
    const std::string& model,
    std::string& err_out)
{
    if (api_key.empty()) { err_out = "密钥为空"; return ""; }

    std::wstring host = (region == "com") ? L"api.agnes-ai.com" : L"api.agnes-ai.cn";
    std::wstring auth = L"Authorization: Bearer " + u2w(api_key);

    std::string body = "{\"model\":\"" + model + "\",\"messages\":" + messages_json + ",\"stream\":false}";

    std::string resp;
    if (!https_post(host, L"/v1/chat/completions", body, auth, resp, err_out)) {
        return "";
    }

    std::string content = extract_content(resp);
    if (content.empty()) {
        err_out = "响应无 content";
        return "";
    }
    return content;
}