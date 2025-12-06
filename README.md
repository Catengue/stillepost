# stillepost
## Overview

This repository provides a proof-of-concept demonstrating how an implant can route its HTTP traffic through a Chromium-based browser by leveraging the Chrome DevTools Protocol. This approach turns the browser into an application-layer proxy without requiring any direct outbound network activity from the implant itself.

Full technical explanation and background:
[https://x90x90.dev/posts/stillepost/](https://x90x90.dev/posts/stillepost/)

## Technique Summary

The implant communicates with a locally running Chromium instance and instructs the browser to perform the actual HTTP requests. This yields several operational advantages:

- Outbound traffic appears as normal browser activity.
- Proxy configuration, authentication, and PAC logic are automatically inherited from the browser.
- Network policies and firewalls commonly allow browser traffic by default.
- The implant avoids creating its own suspicious external connections.

The PoC includes the cJSON library by cJSON, which is used extensively for JSON parsing.

## Repository Structure

**stillepost library (`include/stillepost.*`)**

Exposes three functions:
- `stillepost_init` –> initialize the environment (browser, profile, WebSocket URLs)
- `stillepost` –> issue an HTTP request via the browser
- `stillepost_cleanup` –> shut down the session and free resources

**cJSON dependency (`include/cJSON.*`)**

Bundled for convenience; no external installation required.

**Example client (`main.c`)** 

A ready-to-compile demonstration sending a POST request through edge using stillepost.

**Test web server (`python_code/test_websrv.py`)**

Minimal Python server useful for observing incoming browser-mediated requests during development.

**Original Python PoC (`python_code/stillepost_poc.py`)**
    
The initial proof-of-concept used to check if this idea could really work...

## Integration

To embed stillepost into your own project:
1. Copy the files in the `include/` directory into your codebase.
2. Use `main.c` as a reference implementation for initialization, request invocation, and cleanup.

## Limitations of the Technique
This technique only works when the target web-server allows for [CORS](https://developer.mozilla.org/de/docs/Web/HTTP/Guides/CORS) requests from arbitrary origins. So make sure when using stillepost that your redirector has CORS configured to allow exactly that. While testing the technique, I used a python webserver that explicitly set the following headers:

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: *
```

This is also the reason, why you won't necessarily be able to send arbitrary requests to other web pages in the context of the user. If the target pages don't allow CORS requests, the browser will drop/block the request attempt.