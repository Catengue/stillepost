import http.server
import socketserver
import sys
import os

PORT = 8000
IMAGE_PATH = "./image.png"

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _set_cors_and_custom_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("X-Arbitrary-Header", "ArbitraryValue")

    def _send_body(self):
        body = b"This is the body"
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _print_raw_request(self, body_bytes=b""):
        sys.stdout.write("===== RAW REQUEST BEGIN =====\n")
        sys.stdout.write(self.requestline + "\n")
        sys.stdout.write(str(self.headers))
        if body_bytes:
            sys.stdout.write("\n")
            try:
                sys.stdout.write(body_bytes.decode(errors="replace"))
            except Exception:
                sys.stdout.write(repr(body_bytes))
        sys.stdout.write("\n===== RAW REQUEST END =====\n\n")
        sys.stdout.flush()

    def do_OPTIONS(self):
        self.send_response(200)
        self._set_cors_and_custom_headers()
        self.end_headers()

    def do_GET(self):
        self._print_raw_request()

        if self.path == "/download":
            if not os.path.isfile(IMAGE_PATH):
                self.send_response(404)
                self.end_headers()
                return

            self.send_response(200)
            self._set_cors_and_custom_headers()
            self.send_header("Content-Type", "image/png")
            size = os.path.getsize(IMAGE_PATH)
            self.send_header("Content-Length", str(size))
            self.send_header("Content-Disposition", "attachment; filename=image.png")
            self.end_headers()

            with open(IMAGE_PATH, "rb") as f:
                self.wfile.write(f.read())
            return

        self.send_response(200)
        self._set_cors_and_custom_headers()
        self._send_body()

    def do_POST(self):
        length = self.headers.get("Content-Length")
        if length is not None:
            length = int(length)
            body = self.rfile.read(length)
        else:
            body = b""

        self._print_raw_request(body)

        self.send_response(200)
        self._set_cors_and_custom_headers()
        self._send_body()


with socketserver.TCPServer(("0.0.0.0", PORT), Handler) as httpd:
    print(f"[*] Listening on 0.0.0.0:{PORT}")
    httpd.serve_forever()
