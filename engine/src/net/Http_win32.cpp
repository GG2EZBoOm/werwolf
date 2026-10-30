#include "engine/net/Http.h"

#include "engine/core/Utf8.h"

#include <windows.h>
#include <winhttp.h>

#include <cwchar>
#include <iterator>
#include <memory>

namespace eng::net {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::string lastErrorText(const char* what) {
    const DWORD code = GetLastError();
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return std::string(what) + ": Zeitüberschreitung";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return std::string(what) + ": Server nicht gefunden (keine Internetverbindung?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return std::string(what) + ": Verbindung nicht möglich";
    case ERROR_WINHTTP_SECURE_FAILURE: return std::string(what) + ": Sichere Verbindung fehlgeschlagen (Zertifikat)";
    default: return std::string(what) + " fehlgeschlagen (Fehler " + std::to_string(code) + ")";
    }
}

} // namespace

bool isLocalHttpUrl(const std::string& url) {
    return url.rfind("http://localhost:", 0) == 0 || url.rfind("http://127.0.0.1:", 0) == 0 ||
           url.rfind("http://localhost/", 0) == 0 || url.rfind("http://127.0.0.1/", 0) == 0;
}

static bool httpRequest(const std::string& url, const std::string* json, std::vector<uint8_t>& body,
                        std::string& error, const HttpOptions& options) {
    body.clear();
    const bool secure = url.rfind("https://", 0) == 0;
    if (!secure && !isLocalHttpUrl(url)) {
        error = "Nur sichere HTTPS-Adressen sind erlaubt";
        return false;
    }

    std::wstring wurl = utf8::toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    wchar_t extra[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        error = "Ungültige Adresse: " + url;
        return false;
    }
    if (json && (secure || std::wstring(host) != L"127.0.0.1" ||
                 url.find('@') != std::string::npos || json->size() > 64 * 1024)) {
        error = "Lokale KI erlaubt nur http://127.0.0.1 ohne Zugangsdaten";
        return false;
    }
    const std::wstring object = std::wstring(path) + extra;
    const std::wstring agent = utf8::toWide(options.userAgent);

    Handle session{WinHttpOpen(agent.c_str(), json ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        error = lastErrorText("WinHTTP");
        return false;
    }
    WinHttpSetTimeouts(session.h, options.timeoutMs, options.timeoutMs, options.timeoutMs, options.timeoutMs);
    Handle connection{WinHttpConnect(session.h, host, parts.nPort, 0)};
    if (!connection.h) {
        error = lastErrorText("Verbindung");
        return false;
    }
    Handle request{WinHttpOpenRequest(connection.h, json ? L"POST" : L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) {
        error = lastErrorText("Anfrage");
        return false;
    }
    if (json) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) return false;
    }
    const DWORD requestSize = json ? static_cast<DWORD>(json->size()) : 0;
    if (!WinHttpSendRequest(request.h, json ? L"Content-Type: application/json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
                           json ? static_cast<DWORD>(-1) : 0, json ? const_cast<char*>(json->data()) : WINHTTP_NO_REQUEST_DATA,
                           requestSize, requestSize, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = lastErrorText("Download");
        return false;
    }

    DWORD status = 0;
    DWORD size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        error = status == 404 ? std::string("Datei nicht gefunden (HTTP 404)") : "Server antwortet mit HTTP " + std::to_string(status);
        return false;
    }

    uint64_t total = 0;
    wchar_t lengthText[32] = {};
    DWORD lengthSize = sizeof lengthText;
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                            &lengthSize, WINHTTP_NO_HEADER_INDEX))
        total = std::wcstoull(lengthText, nullptr, 10);
    if (total > options.maxBytes) {
        error = "Datei ist zu groß";
        return false;
    }
    if (total > 0) body.reserve(static_cast<size_t>(total));

    std::vector<uint8_t> chunk(64 * 1024);
    for (;;) {
        if (options.cancel && options.cancel->load()) {
            error = "Abgebrochen";
            return false;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
            error = lastErrorText("Download");
            return false;
        }
        if (read == 0) break;
        body.insert(body.end(), chunk.begin(), chunk.begin() + read);
        if (body.size() > options.maxBytes) {
            error = "Datei ist zu groß";
            return false;
        }
        if (options.progress) options.progress(body.size(), total);
    }
    if (total > 0 && body.size() != total) {
        error = "Download unvollständig";
        return false;
    }
    return true;
}

bool httpGet(const std::string& url, std::vector<uint8_t>& body, std::string& error, const HttpOptions& options) {
    return httpRequest(url, nullptr, body, error, options);
}
bool httpPostLocal(const std::string& url, const std::string& json, std::vector<uint8_t>& body,
                   std::string& error, const HttpOptions& options) {
    return httpRequest(url, &json, body, error, options);
}

} // namespace eng::net
