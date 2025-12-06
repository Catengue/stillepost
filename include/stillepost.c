#include "stillepost.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define TMP_FOLDER_LENGTH 10
#define DEFAULT_DEBUG_PORT 9222

LPSTR g_lpProfileFolder = NULL;
DWORD g_dwDebugPort = 0;
PROCESS_INFORMATION g_piBrowser = {0};
LPSTR g_lpWebSocketURL = NULL;
LPSTR g_lpBrowserPath = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
LPSTR g_lpProfilePath = NULL;

#define USER_AGENT L"StillePostClient/1.0"

static DWORD g_lastError = STILLEPOST_OK;

// Forward declarations for internal functions used by init
void start_browser(void);
LPSTR get_websocket_debugger_url(void);

DWORD stillepost_getError(void) {
    return g_lastError;
}

const char* lpJsTemplate =
    "function sendRequest(method, url, headersJson, dataJson) {\n"
    "    return new Promise(function(resolve) {\n"
    "        var xhr = new XMLHttpRequest();\n"
    "\n"
    "        xhr.onreadystatechange = function () {\n"
    "            if (xhr.readyState === 4) {\n"
    "                var allHeaders = xhr.getAllResponseHeaders() || \"\";\n"
    "                var headerLines = allHeaders.trim().split(/\\r?\\n/);\n"
    "                var hdrObj = {};\n"
    "                for (var i = 0; i < headerLines.length; i++) {\n"
    "                    var line = headerLines[i];\n"
    "                    var idx = line.indexOf(\":\");\n"
    "                    if (idx > -1) {\n"
    "                        var k = line.substring(0, idx).trim();\n"
    "                        var v = line.substring(idx + 1).trim();\n"
    "                        hdrObj[k] = v;\n"
    "                    }\n"
    "                }\n"
    "\n"
    "                var resultObj = {\n"
    "                    status: xhr.status,\n"
    "                    headers: hdrObj,\n"
    "                    body: xhr.responseText\n"
    "                };\n"
    "\n"
    "                resolve(JSON.stringify(resultObj));\n"
    "            }\n"
    "        };\n"
    "\n"
    "        var headers = {};\n"
    "        if (headersJson && typeof headersJson === \"string\") {\n"
    "            try { headers = JSON.parse(headersJson); } catch (_) { resolve(\"\"); return; }\n"
    "        }\n"
    "\n"
    "        var data = {};\n"
    "        if (dataJson && typeof dataJson === \"string\") {\n"
    "            try { data = JSON.parse(dataJson); } catch (_) { data = {}; }\n"
    "        }\n"
    "\n"
    "        if (method === \"GET\" || method === \"HEAD\") {\n"
    "            var params = [];\n"
    "            for (var k in data) {\n"
    "                if (data.hasOwnProperty(k)) {\n"
    "                    params.push(encodeURIComponent(k) + \"=\" + encodeURIComponent(data[k]));\n"
    "                }\n"
    "            }\n"
    "            if (params.length > 0) {\n"
    "                url += (url.indexOf(\"?\") === -1 ? \"?\" : \"&\") + params.join(\"&\");\n"
    "            }\n"
    "            xhr.open(method, url, true);\n"
    "\n"
    "            for (var hk in headers) {\n"
    "                if (headers.hasOwnProperty(hk)) {\n"
    "                    xhr.setRequestHeader(hk, headers[hk]);\n"
    "                }\n"
    "            }\n"
    "\n"
    "            xhr.send();\n"
    "            return;\n"
    "        }\n"
    "\n"
    "        xhr.open(method, url, true);\n"
    "\n"
    "        for (var key in headers) {\n"
    "            if (headers.hasOwnProperty(key)) {\n"
    "                xhr.setRequestHeader(key, headers[key]);\n"
    "            }\n"
    "        }\n"
    "\n"
    "        xhr.send(JSON.stringify(data));\n"
    "    });\n"
    "}\n"
    "\n"
    "sendRequest('__METHOD__', '__URL__', '__HEADERS__', '__DATA__');";

// =============================================================
// ======================= Start Helpers =======================
// =============================================================
void* ReadProcessMemorySafe(HANDLE process, const void* address, SIZE_T szBytes) {
    SIZE_T  bytesRead = 0;
    void   *alloc     = malloc(szBytes);

    if (alloc == NULL) {
        return NULL;
    }

    if (ReadProcessMemory(process, address, alloc, szBytes, &bytesRead) == 0 || bytesRead != szBytes) {
        free(alloc);
        return NULL;
    }

    return alloc;
}

BOOL WriteProcMem(HANDLE process, void* address, const void* data, SIZE_T szBytes) {
    SIZE_T bytesWritten = 0;

    if (WriteProcessMemory(process, address, data, szBytes, &bytesWritten) == 0) {
        return FALSE;
    }

    if (bytesWritten != szBytes) {
        return FALSE;
    }

    return TRUE;
}

static wchar_t* Utf8ToWide(LPCSTR src) {
    if (!src) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (len <= 0) return NULL;

    wchar_t *dst = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!dst) return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, len) <= 0) {
        free(dst);
        return NULL;
    }
    return dst;
}

void RandStr(char *dest, size_t length) {
    char charset[] = "0123456789"
                     "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (length-- > 0) {
        size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
        *dest++ = charset[index];
    }
    *dest = '\0';
}

BOOL SubdirExist(LPSTR lpPath, LPSTR lpSubDir) {
    if (lpPath == NULL || lpSubDir == NULL) {
        return FALSE;
    }

    char szFullPath[MAX_PATH];
    int len = lstrlenA(lpPath);
    const char* format = (len > 0 && lpPath[len - 1] == '\\') ? "%s%s" : "%s\\%s";

    if (snprintf(szFullPath, MAX_PATH, format, lpPath, lpSubDir) >= MAX_PATH) return FALSE;

    DWORD dwAttrib = GetFileAttributesA(szFullPath);

    return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

BOOL DeleteDirectoryRecursively(LPSTR lpPath) {
    SHFILEOPSTRUCTA file_op = {0};
    size_t len = 0;

    if (FAILED(StringCchLengthA(lpPath, MAX_PATH, &len))) {
        return FALSE;
    }

    size_t bufferSize = len + 2;
    char* pBuffer = (char*)malloc(bufferSize);

    if (!pBuffer) return FALSE;

    if (FAILED(StringCchCopyA(pBuffer, bufferSize, lpPath))) {
        free(pBuffer);
        return FALSE;
    }

    pBuffer[len + 1] = '\0';

    file_op.hwnd = NULL;
    file_op.wFunc = FO_DELETE;
    file_op.pFrom = pBuffer;
    file_op.pTo = NULL;
    file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    file_op.fAnyOperationsAborted = FALSE;
    file_op.hNameMappings = NULL;
    file_op.lpszProgressTitle = NULL;

    int result = SHFileOperationA(&file_op);

    free(pBuffer);
    return (result == 0);
}

char *ReplaceFirst(const char *src, const char *needle, const char *replacement) {
    const char *p = strstr(src, needle);
    if (!p)
        return NULL;

    size_t src_len = strlen(src);
    size_t needle_len = strlen(needle);
    size_t repl_len  = strlen(replacement);

    size_t new_len = src_len - needle_len + repl_len;

    char *out = malloc(new_len + 1);
    if (!out)
        return NULL;

    size_t prefix_len = p - src;

    memcpy(out, src, prefix_len);
    memcpy(out + prefix_len, replacement, repl_len);

    const char *suffix = p + needle_len;
    size_t suffix_len = src_len - prefix_len - needle_len;

    memcpy(out + prefix_len + repl_len, suffix, suffix_len);

    out[new_len] = '\0';
    return out;
}

static BOOL ParseWsUrl(const wchar_t *wsUrl, BOOL *pSecure, wchar_t **pHost, INTERNET_PORT *pPort, wchar_t **pPath) {
    if (!wsUrl || !pSecure || !pHost || !pPort || !pPath) return FALSE;

    const wchar_t *p = wsUrl;
    BOOL secure = FALSE;

    if (wcsncmp(p, L"ws://", 5) == 0) {
        secure = FALSE;
        p += 5;
    } else if (wcsncmp(p, L"wss://", 6) == 0) {
        secure = TRUE;
        p += 6;
    } else {
        return FALSE;
    }

    const wchar_t *hostStart = p;
    const wchar_t *slash = wcschr(hostStart, L'/');
    if (!slash) return FALSE;

    const wchar_t *hostEnd = slash;
    const wchar_t *colon = NULL;

    // search ':' between hostStart and slash
    for (const wchar_t *c = hostStart; c < slash; ++c) {
        if (*c == L':') {
            colon = c;
            break;
        }
    }

    INTERNET_PORT port;
    if (colon) {
        hostEnd = colon;
        wchar_t portBuf[16] = {0};
        size_t portLen = (size_t)(slash - colon - 1);
        if (portLen == 0 || portLen >= sizeof(portBuf) / sizeof(portBuf[0])) return FALSE;
        wcsncpy_s(portBuf, _countof(portBuf), colon + 1, portLen);
        port = (INTERNET_PORT)_wtoi(portBuf);
        if (port == 0) return FALSE;
    } else {
        port = secure ? 443 : 80;
    }

    size_t hostLen = (size_t)(hostEnd - hostStart);
    wchar_t *host = (wchar_t*)malloc((hostLen + 1) * sizeof(wchar_t));
    if (!host) return FALSE;
    wcsncpy_s(host, hostLen + 1, hostStart, hostLen);
    host[hostLen] = L'\0';

    // include leading '/'
    const wchar_t *pathStart = slash;
    size_t pathLen = wcslen(pathStart);
    wchar_t *path = (wchar_t*)malloc((pathLen + 1) * sizeof(wchar_t));
    if (!path) {
        free(host);
        return FALSE;
    }
    wcsncpy_s(path, pathLen + 1, pathStart, pathLen);
    path[pathLen] = L'\0';

    *pSecure = secure;
    *pHost = host;
    *pPort = port;
    *pPath = path;
    return TRUE;
}

// =============================================================
// ======================== End Helpers ========================
// =============================================================

// Main Logic & Functions ====================

void start_browser(void) {
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    char debugCmd[2048];

    HRESULT hr = StringCchPrintfA(
        debugCmd,
        sizeof(debugCmd),
        "\"%s\" --remote-debugging-port=%d --headless --user-data-dir=\"%s\" --log-level=3 --disable-logging",
        g_lpBrowserPath,
        g_dwDebugPort,
        g_lpProfileFolder
    );
    if (FAILED(hr))
        return;

    if (!CreateProcessA(
        NULL,
        debugCmd,
        NULL,
        NULL,
        FALSE,
        DETACHED_PROCESS,
        NULL,
        NULL,
        &si,
        &pi))
        return;

    g_piBrowser = pi;
    CloseHandle(pi.hThread);
}

LPSTR get_websocket_debugger_url(void) {
    HINTERNET hSession  = NULL;
    HINTERNET hConnect  = NULL;
    HINTERNET hRequest  = NULL;
    LPSTR     lpResponse  = NULL;
    DWORD     dwResponseSize = 0;
    LPSTR     lpWsUrl    = NULL;  // return value

    hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        goto cleanup;

    hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)g_dwDebugPort, 0);
    if (!hConnect)
        goto cleanup;

    hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/json/list", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest)
        goto cleanup;

    if (!WinHttpSendRequest(hRequest, NULL, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto cleanup;

    if (!WinHttpReceiveResponse(hRequest, NULL))
        goto cleanup;

    for (;;) {
        BYTE  bBuf[4096];
        DWORD dwBytesRead = 0;

        if (!WinHttpReadData(hRequest, bBuf, sizeof(bBuf), &dwBytesRead))
            goto cleanup;

        if (dwBytesRead == 0)
            break;

        LPSTR lpNewResp = (char *)realloc(lpResponse, dwResponseSize + dwBytesRead + 1);
        if (!lpNewResp)
            goto cleanup;

        lpResponse = lpNewResp;
        memcpy(lpResponse + dwResponseSize, bBuf, dwBytesRead);
        dwResponseSize += dwBytesRead;
        lpResponse[dwResponseSize] = '\0';
    }

    if (!lpResponse)
        goto cleanup;

    /* Parse JSON: resp.json()[0]["webSocketDebuggerUrl"] */
    cJSON *cjsonRoot = cJSON_Parse(lpResponse);
    if (!cjsonRoot)
        goto cleanup;

    if (!cJSON_IsArray(cjsonRoot)) {
        cJSON_Delete(cjsonRoot);
        goto cleanup;
    }

    cJSON *cjsonFirstElem = cJSON_GetArrayItem(cjsonRoot, 0);
    if (!cjsonFirstElem || !cJSON_IsObject(cjsonFirstElem)) {
        cJSON_Delete(cjsonRoot);
        goto cleanup;
    }

    cJSON *cjsonWsItem = cJSON_GetObjectItemCaseSensitive(cjsonFirstElem, "webSocketDebuggerUrl");
    if (!cjsonWsItem || !cJSON_IsString(cjsonWsItem) || !cjsonWsItem->valuestring) {
        cJSON_Delete(cjsonRoot);
        goto cleanup;
    }

    lpWsUrl = _strdup(cjsonWsItem->valuestring);
    cJSON_Delete(cjsonRoot);

cleanup:
    if (hRequest)
        WinHttpCloseHandle(hRequest);
    if (hConnect)
        WinHttpCloseHandle(hConnect);
    if (hSession)
        WinHttpCloseHandle(hSession);

    if (lpResponse)
        free(lpResponse);

    return lpWsUrl;
}

BOOL stillepost_init(LPSTR lpBrowserPath, DWORD dwDebugPort, LPSTR lpProfilePath) {
    g_lastError = STILLEPOST_OK;
    g_dwDebugPort = dwDebugPort;

    srand(time(NULL));

    if (!dwDebugPort) {
        g_dwDebugPort = rand()%65000;
    }
    if (lpBrowserPath) g_lpBrowserPath = lpBrowserPath;
    if (lpProfilePath) g_lpProfilePath = lpProfilePath;

    BOOL ok = FALSE;
    char tmpName[TMP_FOLDER_LENGTH + 1] = {0};

    if (!g_lpProfilePath) {
        // allocate profile folder buffer
        g_lpProfileFolder = (LPSTR)malloc(sizeof(char) * (MAX_PATH + 1));
        if (!g_lpProfileFolder) {
            print_error("Failed to allocate memory for profile folder path");
            g_lastError = STILLEPOST_ERR_ALLOC_PROFILE_PATH;
            goto done;
        }
        memset(g_lpProfileFolder, 0, MAX_PATH + 1);

        // get base temp path
        if (!GetTempPathA(MAX_PATH - 10, g_lpProfileFolder)) {
            print_error("Failed to get temp path");
            g_lastError = STILLEPOST_ERR_GET_TEMP_PATH;
            goto done;
        }

        // create random subfolder name that does not exist
        srand((unsigned int)time(NULL));
        RandStr(tmpName, TMP_FOLDER_LENGTH);
        while (SubdirExist(g_lpProfileFolder, tmpName)) {
            RandStr(tmpName, TMP_FOLDER_LENGTH);
        }

        // append subfolder name to temp path
        if (FAILED(StringCchCatA(g_lpProfileFolder, MAX_PATH, tmpName))) {
            print_error("Failed to compose profile folder path");
            g_lastError = STILLEPOST_ERR_COMPOSE_PROFILE_PATH;
            goto done;
        }
    }

    print_info("Using profile folder: %s", g_lpProfileFolder);
    print_info("Using debug port: %lu", g_dwDebugPort);

    // start browser
    print_info("Starting Chrome...");
    start_browser();
    if (!g_piBrowser.hProcess) {
        print_error("Failed to start Chrome");
        g_lastError = STILLEPOST_ERR_START_BROWSER;
        goto done;
    }
    print_success("Chrome started (PID: %lu)", g_piBrowser.dwProcessId);

    // fetch websocket url
    g_lpWebSocketURL = get_websocket_debugger_url();
    if (!g_lpWebSocketURL) {
        print_error("Could not get websocket URL");
        g_lastError = STILLEPOST_ERR_FETCH_WS_URL;
        goto done;
    }
    print_success("Got websocket URL: '%s'", g_lpWebSocketURL);

    ok = TRUE;

done:
    if (!ok) {
        // partial cleanup on failure
        if (g_piBrowser.hProcess) {
            TerminateProcess(g_piBrowser.hProcess, 0);
            WaitForSingleObject(g_piBrowser.hProcess, 5000);
            CloseHandle(g_piBrowser.hProcess);
            ZeroMemory(&g_piBrowser, sizeof(g_piBrowser));
        }
        if (g_lpWebSocketURL) { free(g_lpWebSocketURL); g_lpWebSocketURL = NULL; }
        if (g_lpProfileFolder) {
            // try delete profile folder if it exists and was created/used
            DeleteDirectoryRecursively(g_lpProfileFolder);
            free(g_lpProfileFolder);
            g_lpProfileFolder = NULL;
        }
    }
    return ok;
}

response_t* stillepost(LPSTR lpMethod, LPSTR lpURL, cJSON *cjsonpHeaders, cJSON *cjsonpData) {
    g_lastError = STILLEPOST_OK;
    response_t *outResp = NULL;
    print_info("Building JS payload");
    LPSTR insHeaders;
    LPSTR lpJsPayload;
    char *recvBuf = NULL;

    // 1) "Parse" the JS template
    LPSTR insMethod  = ReplaceFirst(lpJsTemplate, "__METHOD__",  lpMethod);
    LPSTR insURL     = ReplaceFirst(insMethod,   "__URL__",      lpURL);
    if (!cjsonpHeaders) {
        insHeaders = ReplaceFirst(insURL,      "__HEADERS__",  "\"\"");
    } else {
        insHeaders = ReplaceFirst(insURL,      "__HEADERS__",  cJSON_PrintUnformatted(cjsonpHeaders));
    }
    if (!cjsonpData) {
        lpJsPayload = ReplaceFirst(insHeaders,  "__DATA__",     "\"\"");
    } else {
        lpJsPayload = ReplaceFirst(insHeaders,  "__DATA__",     cJSON_PrintUnformatted(cjsonpData));
    }

    free(insMethod);
    free(insURL);
    free(insHeaders);

    // 2) Build the DevTools protocol JSON message:
    // {
    //   "id": 1,
    //   "method": "Runtime.evaluate",
    //   "params": {
    //      "expression": "<lpJsPayload>",
    //      "awaitPromise": true,
    //      "returnByValue": true
    //   }
    // }
    cJSON *cjsonRoot   = cJSON_CreateObject();
    cJSON *cjsonParams = cJSON_CreateObject();
    cJSON *cjsonRespJson = NULL;

    cJSON_AddNumberToObject(cjsonRoot, "id", 1);
    cJSON_AddStringToObject(cjsonRoot, "method", "Runtime.evaluate");
    cJSON_AddItemToObject(cjsonRoot, "params", cjsonParams);

    cJSON_AddStringToObject(cjsonParams, "expression", lpJsPayload);
    cJSON_AddBoolToObject(cjsonParams,   "awaitPromise", 1);
    cJSON_AddBoolToObject(cjsonParams,   "returnByValue", 1);

    LPSTR lpPayloadStr = cJSON_PrintUnformatted(cjsonRoot);

    cJSON_Delete(cjsonRoot);
    free(lpJsPayload);

    if (!lpPayloadStr) {
        print_error("Failed to build JSON payload");
        g_lastError = STILLEPOST_ERR_BUILD_JSON_PAYLOAD;
        return NULL;
    }

    print_info("Sending '%s' request to '%s'", lpMethod, lpURL);

    // 3) Convert ws_url (UTF-8) to wide and parse it
    wchar_t *wsUrlW = Utf8ToWide(g_lpWebSocketURL);
    if (!wsUrlW) {
        print_error("Failed to convert ws_url to wide");
        g_lastError = STILLEPOST_ERR_WSURL_TO_WIDE;
        free(lpPayloadStr);
        return NULL;
    }

    BOOL secure = FALSE;
    wchar_t *hostW = NULL;
    wchar_t *pathW = NULL;
    INTERNET_PORT port = 0;

    if (!ParseWsUrl(wsUrlW, &secure, &hostW, &port, &pathW)) {
        print_error("Failed to parse ws_url: %S", wsUrlW);
        g_lastError = STILLEPOST_ERR_PARSE_WSURL;
        free(wsUrlW);
        free(lpPayloadStr);
        return NULL;
    }

    // 4) Open WinHTTP session and WebSocket
    HINTERNET hSession   = NULL;
    HINTERNET hConnect   = NULL;
    HINTERNET hRequest   = NULL;
    HINTERNET hWebSocket = NULL;

    hSession = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        print_error("WinHttpOpen failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WINHTTP_OPEN;
        goto cleanup;
    }

    hConnect = WinHttpConnect(hSession, hostW, port, 0);
    if (!hConnect) {
        print_error("WinHttpConnect failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WINHTTP_CONNECT;
        goto cleanup;
    }

    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;

    hRequest = WinHttpOpenRequest(hConnect, L"GET", pathW, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hRequest) {
        print_error("WinHttpOpenRequest failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WINHTTP_OPENREQUEST;
        goto cleanup;
    }
    
    if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        print_error("WinHttpSetOption(WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET) failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WINHTTP_SET_WEBSOCKET;
        goto cleanup;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        print_error("WinHttpSendRequest failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WINHTTP_SENDREQUEST;
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        print_error("WinHttpReceiveResponse failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WINHTTP_RECEIVERESPONSE;
        goto cleanup;
    }

    hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
    if (!hWebSocket) {
        print_error("WinHttpWebSocketCompleteUpgrade failed: %lu", GetLastError());
        g_lastError = STILLEPOST_ERR_WEBSOCKET_UPGRADE;
        goto cleanup;
    }

    WinHttpCloseHandle(hRequest);
    hRequest = NULL;

    // 5) Send the JSON message over WebSocket
    DWORD dwError = WinHttpWebSocketSend(hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)lpPayloadStr, (DWORD)strlen(lpPayloadStr));
    if (dwError != ERROR_SUCCESS) {
        print_error("WinHttpWebSocketSend failed: %lu", dwError);
        g_lastError = STILLEPOST_ERR_WEBSOCKET_SEND;
        goto cleanup;
    }

    free(lpPayloadStr);
    lpPayloadStr = NULL;

    // 6) Receive messaeg dynamically (had to add this to handle fragmentation and large payloads)
    size_t capacity = 8192;
    size_t total = 0;
    recvBuf = (char*)malloc(capacity);
    if (!recvBuf) {
        print_error("Out of memory allocating receive buffer");
        g_lastError = STILLEPOST_ERR_OUT_OF_MEMORY_RECVBUF;
        goto cleanup;
    }

    DWORD dwBytesRead = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE eBufferType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;

    for (;;) {
        // Ensure we have some free space, leave 1 byte for NUL
        if (capacity - total < 4096) {
            size_t newCap = capacity * 2;
            char *tmp = (char*)realloc(recvBuf, newCap);
            if (!tmp) {
                print_error("Out of memory growing receive buffer");
                g_lastError = STILLEPOST_ERR_OUT_OF_MEMORY_RECVBUF_GROW;
                goto cleanup;
            }
            recvBuf = tmp;
            capacity = newCap;
        }

        dwError = WinHttpWebSocketReceive(
            hWebSocket,
            recvBuf + total,
            (DWORD)(capacity - total - 1),
            &dwBytesRead,
            &eBufferType
        );
        if (dwError != ERROR_SUCCESS) {
            print_error("WinHttpWebSocketReceive failed: %lu", dwError);
            g_lastError = STILLEPOST_ERR_WEBSOCKET_RECEIVE;
            goto cleanup;
        }

        if (eBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            print_error("WebSocket closed while receiving response");
            g_lastError = STILLEPOST_ERR_WEBSOCKET_CLOSED;
            goto cleanup;
        }

        total += dwBytesRead;

        if (eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            break; // final chunk of a UTF-8 message received
        } else if (eBufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
            continue; // more fragments to come
        } else {
            // Unexpected buffer type for this protocol usage
            print_error("Unexpected WebSocket buffer type: %u", (unsigned)eBufferType);
            g_lastError = STILLEPOST_ERR_WEBSOCKET_BUFFER_TYPE;
            goto cleanup;
        }
    }

    recvBuf[total] = '\0';

    // 7) Parse JSON response from DevTools and build response_t
    cjsonRespJson = cJSON_Parse(recvBuf);
    if (!cjsonRespJson) {
        print_error("Failed to parse JSON response");
        g_lastError = STILLEPOST_ERR_PARSE_JSON_RESPONSE;
        goto cleanup;
    }

    cJSON *first = cJSON_GetObjectItem(cjsonRespJson, "result");
    if (!cJSON_IsObject(first)) {
        print_error("Failed to parse JSON response (no 'result')");
        g_lastError = STILLEPOST_ERR_PARSE_JSON_NO_RESULT;
        goto cleanup;
    }

    const cJSON *second = cJSON_GetObjectItem(first, "result");
    if (!cJSON_IsObject(second)) {
        print_error("Failed to parse JSON response (no 'result.result')");
        goto cleanup;
    }

    const cJSON *value = cJSON_GetObjectItem(second, "value");
    if (!cJSON_IsString(value) || !value->valuestring) {
        print_error("Failed to parse JSON response (no 'value' string)");
        goto cleanup;
    }

    // value->valuestring contains a JSON string like:
    // {"status":200,"headers":{...},"body":"..."}
    cJSON *valueJson = cJSON_Parse(value->valuestring);
    if (!valueJson) {
        print_error("Failed to parse inner JSON value");
        goto cleanup;
    }

    outResp = (response_t*)calloc(1, sizeof(response_t));
    if (!outResp) {
        print_error("Out of memory allocating response_t");
        cJSON_Delete(valueJson);
        goto cleanup;
    }

    cJSON *jStatus  = cJSON_GetObjectItem(valueJson, "status");
    cJSON *jHeaders = cJSON_GetObjectItem(valueJson, "headers");
    cJSON *jBody    = cJSON_GetObjectItem(valueJson, "body");

    if (cJSON_IsNumber(jStatus)) {
        // valuedouble is fine here; cast to DWORD
        outResp->dwStatusCode = (DWORD)(jStatus->valuedouble);
    } else if (cJSON_IsString(jStatus) && jStatus->valuestring) {
        outResp->dwStatusCode = (DWORD)strtoul(jStatus->valuestring, NULL, 10);
    } else {
        outResp->dwStatusCode = 0;
    }

    if (cJSON_IsObject(jHeaders)) {
        // Deep copy headers object so caller owns it
        outResp->cjsonpHeaders = cJSON_Duplicate(jHeaders, 1);
    } else {
        outResp->cjsonpHeaders = cJSON_CreateObject();
    }

    if (cJSON_IsString(jBody) && jBody->valuestring) {
        size_t blen = strlen(jBody->valuestring);
        outResp->lpBody = (LPSTR)malloc(blen + 1);
        if (outResp->lpBody) {
            memcpy(outResp->lpBody, jBody->valuestring, blen + 1);
        }
    } else {
        outResp->lpBody = NULL;
    }

    cJSON_Delete(valueJson);

cleanup:
    if (hWebSocket) {
        WinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        WinHttpCloseHandle(hWebSocket);
    }
    if (hRequest)  WinHttpCloseHandle(hRequest);
    if (hConnect)  WinHttpCloseHandle(hConnect);
    if (hSession)  WinHttpCloseHandle(hSession);

    if (hostW) free(hostW);
    if (pathW) free(pathW);
    if (wsUrlW) free(wsUrlW);
    if (lpPayloadStr) free(lpPayloadStr);
    if (cjsonRespJson) cJSON_Delete(cjsonRespJson);
    if (recvBuf) free(recvBuf);

    // Return the built response on success, otherwise NULL
    return outResp;
}

void stillepost_cleanup(void) {
    // terminate browser if running
    if (g_piBrowser.hProcess) {
        if (TerminateProcess(g_piBrowser.hProcess, 0)) {
            print_success("Chrome terminated");
            WaitForSingleObject(g_piBrowser.hProcess, 5000);
        } else {
            print_error("Failed to terminate Chrome");
        }
        CloseHandle(g_piBrowser.hProcess);
        ZeroMemory(&g_piBrowser, sizeof(g_piBrowser));
    }

    // delete profile folder with retries as it may be locked momentarily
    if (g_lpProfileFolder) {
        int max_retries = 5;
        BOOL deleted = FALSE;
        for (int i = 0; i < max_retries; i++) {
            if (DeleteDirectoryRecursively(g_lpProfileFolder)) {
                print_success("Successfully deleted profile folder");
                deleted = TRUE;
                break;
            }
            Sleep(500);
        }
        if (!deleted) {
            print_error("Failed to delete profile folder (it may still be locked): '%s'", g_lpProfileFolder);
        }
        free(g_lpProfileFolder);
        g_lpProfileFolder = NULL;
    }

    if (g_lpWebSocketURL) { free(g_lpWebSocketURL); g_lpWebSocketURL = NULL; }
    g_dwDebugPort = 0;
}