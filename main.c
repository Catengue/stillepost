#include <stdio.h>

#include "include/stillepost.h"

int main() {
    // Initialize the stillepost runtime (allocs, temp folder, start Edge, fetch ws URL)
    if (!stillepost_init(NULL, 0, NULL)) {
        printf("[!] Initialization failed: %lu\n", stillepost_getError());
        return 1;
    }

    // Prepare headers and data for the request
    cJSON *cjsonpHttpHeaders = cJSON_CreateObject();
    cJSON_AddStringToObject(cjsonpHttpHeaders, "X-Poc", "SomeArbitraryValue");

    cJSON *cjsonpData = cJSON_CreateObject();
    cJSON_AddStringToObject(cjsonpData, "param1", "value1");
    cJSON_AddStringToObject(cjsonpData, "param2", "value2");

    // Send the request via stillepost
    response_t *resp = stillepost("GET", "http://192.168.157.133:8000/", cjsonpHttpHeaders, cjsonpData);
    if (resp) {
        printf("[i] -> Returned status code: %lu\n", resp->dwStatusCode);
        printf("[i] -> Returned headers: %s\n", cJSON_PrintUnformatted(resp->cjsonpHeaders));
        printf("[i] -> Returned body: %s\n", resp->lpBody);
    } else {
        printf("[!] Something went wrong: %lu\n", stillepost_getError());
    }

    // Cleanup stillepost internal resources
    stillepost_cleanup();

    // Cleanup main-owned resources
    if (cjsonpHttpHeaders) cJSON_Delete(cjsonpHttpHeaders);
    if (cjsonpData) cJSON_Delete(cjsonpData);

    return 0;
}