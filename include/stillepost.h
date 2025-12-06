#ifndef STILLEPOST_C_STILLEPOST_H
#define STILLEPOST_C_STILLEPOST_H

#include <windows.h>
#include <stdio.h>
#include <winhttp.h>
#include <strsafe.h>
#include <winternl.h>

#include "cJSON.h"

#define DEBUG_ENABLED

#ifdef DEBUG_ENABLED
#define print_info(fmt, ...)    printf("[i] " fmt "\n", ##__VA_ARGS__)
#define print_success(fmt, ...) printf("[+] " fmt "\n", ##__VA_ARGS__)
#define print_error(fmt, ...)   fprintf(stderr, "[!] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef DEBUG_ENABLED
#define print_info(fmt, ...)    //
#define print_success(fmt, ...) //
#define print_error(fmt, ...)   //
#endif

// Error codes
// Keep values stable for external users.
typedef enum {
    STILLEPOST_OK = 0,
    STILLEPOST_ERR_ALLOC_PROFILE_PATH = 1,
    STILLEPOST_ERR_GET_TEMP_PATH = 2,
    STILLEPOST_ERR_COMPOSE_PROFILE_PATH = 3,
    STILLEPOST_ERR_START_BROWSER = 4,
    STILLEPOST_ERR_FETCH_WS_URL = 5,
    STILLEPOST_ERR_TERMINATE_CHROME_FAILED = 6,
    STILLEPOST_ERR_DELETE_PROFILE_FOLDER_FAILED = 7,
    STILLEPOST_ERR_BUILD_JSON_PAYLOAD = 8,
    STILLEPOST_ERR_WSURL_TO_WIDE = 9,
    STILLEPOST_ERR_PARSE_WSURL = 10,
    STILLEPOST_ERR_WINHTTP_OPEN = 11,
    STILLEPOST_ERR_WINHTTP_CONNECT = 12,
    STILLEPOST_ERR_WINHTTP_OPENREQUEST = 13,
    STILLEPOST_ERR_WINHTTP_SET_WEBSOCKET = 14,
    STILLEPOST_ERR_WINHTTP_SENDREQUEST = 15,
    STILLEPOST_ERR_WINHTTP_RECEIVERESPONSE = 16,
    STILLEPOST_ERR_WEBSOCKET_UPGRADE = 17,
    STILLEPOST_ERR_WEBSOCKET_SEND = 18,
    STILLEPOST_ERR_OUT_OF_MEMORY_RECVBUF = 19,
    STILLEPOST_ERR_OUT_OF_MEMORY_RECVBUF_GROW = 20,
    STILLEPOST_ERR_WEBSOCKET_RECEIVE = 21,
    STILLEPOST_ERR_WEBSOCKET_CLOSED = 22,
    STILLEPOST_ERR_WEBSOCKET_BUFFER_TYPE = 23,
    STILLEPOST_ERR_PARSE_JSON_RESPONSE = 24,
    STILLEPOST_ERR_PARSE_JSON_NO_RESULT = 25,
    STILLEPOST_ERR_PARSE_JSON_NO_RESULT_RESULT = 26,
    STILLEPOST_ERR_PARSE_JSON_NO_VALUE_STRING = 27,
    STILLEPOST_ERR_PARSE_INNER_JSON_VALUE = 28,
    STILLEPOST_ERR_ALLOC_RESPONSE_T = 29
} STILLEPOST_ERROR;

// Opaque internal state is managed inside stillepost.c
// Public API

typedef struct {
    DWORD dwStatusCode;
    cJSON *cjsonpHeaders;
    LPSTR lpBody;
} response_t;

typedef NTSTATUS(*NtQueryInformationProcess2)(
    IN HANDLE,
    IN PROCESSINFOCLASS,
    OUT PVOID,
    IN ULONG,
    OUT PULONG
    );

// Returns the last error code set by the library.
DWORD stillepost_getError(void);

// Initializes internal state: allocates globals, prepares temp folder,
// starts Edge with remote debugging and stores the DevTools WebSocket URL.
// Returns TRUE on success, FALSE on failure.
BOOL stillepost_init(LPSTR lpBrowserPath, DWORD dwDebugPort, LPSTR lpProfilePath);

// Sends a request via the DevTools Runtime.evaluate pathway using prepared headers and data.
response_t* stillepost(LPSTR lpMethod, LPSTR lpURL, cJSON* cjsonpHeaders, cJSON* cjsonpData);

// Cleans up: kills Edge, deletes temp folder, frees internal allocations.
void stillepost_cleanup(void);

#endif //STILLEPOST_C_STILLEPOST_H