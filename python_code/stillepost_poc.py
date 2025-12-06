import random
import shutil
import string
import subprocess
import requests
import time
import json
import socket
import websocket

global profile_folder
global debug_port
global chrome_process
global ws_url

javaScript_wrapper = """
function sendRequest(method, url, headersJson, dataJson) {
    return new Promise(function(resolve) {
        var xhr = new XMLHttpRequest();

        xhr.onreadystatechange = function () {
            if (xhr.readyState === 4) {
                var allHeaders = xhr.getAllResponseHeaders() || "";
                var headerLines = allHeaders.trim().split(/\\r?\\n/);
                var hdrObj = {};
                for (var i = 0; i < headerLines.length; i++) {
                    var line = headerLines[i];
                    var idx = line.indexOf(":");
                    if (idx > -1) {
                        var k = line.substring(0, idx).trim();
                        var v = line.substring(idx + 1).trim();
                        hdrObj[k] = v;
                    }
                }

                var resultObj = {
                    status: xhr.status,
                    headers: hdrObj,
                    body: xhr.responseText
                };

                resolve(JSON.stringify(resultObj));
            }
        };

        var headers = {};
        if (headersJson && typeof headersJson === "string") {
            try { headers = JSON.parse(headersJson); } catch (_) { resolve(""); return; }
        }

        var data = {};
        if (dataJson && typeof dataJson === "string") {
            try { data = JSON.parse(dataJson); } catch (_) { data = {}; }
        }

        if (method === "GET" || method === "HEAD") {
            var params = [];
            for (var k in data) {
                if (data.hasOwnProperty(k)) {
                    params.push(encodeURIComponent(k) + "=" + encodeURIComponent(data[k]));
                }
            }
            if (params.length > 0) {
                url += (url.indexOf("?") === -1 ? "?" : "&") + params.join("&");
            }
            xhr.open(method, url, true);

            for (var hk in headers) {
                if (headers.hasOwnProperty(hk)) {
                    xhr.setRequestHeader(hk, headers[hk]);
                }
            }

            xhr.send();
            return;
        }

        xhr.open(method, url, true);

        for (var key in headers) {
            if (headers.hasOwnProperty(key)) {
                xhr.setRequestHeader(key, headers[key]);
            }
        }

        xhr.send(JSON.stringify(data));
    });
}

sendRequest(__METHOD__, __URL__, __HEADERS__, __DATA__);
"""


def wait_for_port(host, port, timeout_sec=10):
    start = time.time()
    while time.time() - start < timeout_sec:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.settimeout(0.5)
            s.connect((host, port))
            s.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def start_browser():
    global profile_folder
    global debug_port
    global chrome_process

    browser_path = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"

    print("[*] Starting browser")
    process = subprocess.Popen([
        browser_path,
        f"--remote-debugging-port={debug_port}",
        "--user-data-dir={}".format(profile_folder),
        "--headless"
    ])

    chrome_process = process
    print("[+] Browser started (PID: {})".format(chrome_process.pid))


def cleanup(timeout_sec=5):
    print("[*] Cleaning up")
    global profile_folder
    global chrome_process

    chrome_process.kill()
    start = time.time()
    while time.time() - start < timeout_sec:
        try:
            shutil.rmtree("{}".format(profile_folder))
            return True
        except OSError:
            time.sleep(0.2)

    print("[+] Clean up done")
    return False


def stillepost(method, url, headersJson, dataJson):
    global ws_url

    ws = websocket.create_connection(ws_url, suppress_origin=True)

    js = (
        javaScript_wrapper
        .replace("__METHOD__", json.dumps(method))
        .replace("__URL__", json.dumps(url))
        .replace("__HEADERS__", json.dumps(json.dumps(headersJson)))
        .replace("__DATA__", json.dumps(json.dumps(dataJson)))
    )

    msg_id = 1
    payload = {
        "id": msg_id,
        "method": "Runtime.evaluate",
        "params": {
            "expression": js,
            "awaitPromise": True,
            "returnByValue": True
        },
    }

    ws.send(json.dumps(payload))
    result_raw = ws.recv()
    ws.close()

    result = json.loads(result_raw)
    return result


def main():
    global profile_folder
    global debug_port
    global ws_url

    profile_folder = "C:\\Temp\\" + ''.join(random.choice(string.ascii_letters) for i in range(10))

    debug_port = random.randint(9090, 9090)
    print("[i] Debug port: ", debug_port)
    print("[i] Profile folder: ", profile_folder)

    start_browser()

    if not wait_for_port("127.0.0.1", debug_port, timeout_sec=10):
        print("[!] Debug port not reachable")
        return

    resp = requests.get(f"http://127.0.0.1:{debug_port}/json/list")
    ws_url = resp.json()[0]["webSocketDebuggerUrl"]

    target_url = "http://<URL HERE>" # <------------ Change this to your target URL!
    target_method = "POST"
    target_headers = {"X-Poc": "SomeArbitraryValue"}
    target_data = {"param1":"value1", "param2":"value2"}

    print("[i] Target: {} -> {}".format(target_method, target_url))
    print("[*] Sending request...")
    ret = stillepost(target_method, target_url, target_headers, target_data)

    srv_response = json.loads(ret["result"]["result"]["value"])

    print(json.dumps(srv_response, indent=4))

    if srv_response["status"] == 0:
        print("[!] Something went wrong sending the request (maybe the target isn`t reachable?).")
    else:
        print("[+] Response Code: ", srv_response["status"])
        print("[+] Response Headers: ", srv_response["headers"])
        print("[+] Response Body: ", srv_response["body"])

    cleanup()

if __name__ == "__main__":
    main()
