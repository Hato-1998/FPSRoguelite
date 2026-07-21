#!/usr/bin/env python3
"""
vibeue-proxy.py -- Local MCP reverse proxy for the VibeUE Unreal Editor plugin.

The VibeUE plugin hosts an MCP "Streamable HTTP" server at 127.0.0.1:8088/mcp,
but only while the Unreal Editor process is alive. When the editor closes, an
MCP client connected directly to 8088 loses its tool list and cannot recover
without restarting its whole session.

This proxy sits at 127.0.0.1:8089/mcp and stays up independently of the editor:
  * Editor running -> transparently relay every request to the upstream server.
  * Editor closed  -> serve "initialize"/"tools/list" from a cached manifest
                       so the client keeps its tools, and answer "tools/call"
                       with a friendly message instead of a dead connection.

This is a dumb pipe with an offline fallback, not a reimplementation of the
plugin. Standard library only -- no third-party dependencies.
"""

import json
import os
import socket
import sys
import traceback
import urllib.error
import urllib.parse
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ---- Configuration: module-level constants, each overridable by an env var.

# The proxy always binds to loopback only. This is NOT configurable: exposing
# it on a real interface would let anyone on the network drive the editor.
LISTEN_HOST = "127.0.0.1"

LISTEN_PORT = int(os.environ.get("VIBEUE_PROXY_PORT", "8089"))
UPSTREAM_HOST = os.environ.get("VIBEUE_UPSTREAM_HOST", "127.0.0.1")
UPSTREAM_PORT = int(os.environ.get("VIBEUE_UPSTREAM_PORT", "8088"))

_appdata = os.environ.get("APPDATA")
if _appdata:
    _default_manifest = os.path.join(_appdata, "VibeUE", "tools-manifest.json")
else:
    _default_manifest = os.path.join(os.path.expanduser("~"), ".config", "VibeUE", "tools-manifest.json")
MANIFEST_PATH = os.environ.get("VIBEUE_MANIFEST", _default_manifest)

# Connect timeout used only to answer "is the editor up?"; kept short so
# offline detection never makes the client wait long.
PROBE_TIMEOUT = float(os.environ.get("VIBEUE_PROBE_TIMEOUT", "1.0"))

# Read timeout for a relayed call; long, because editor-side operations
# (asset builds, blueprint compiles, etc.) can legitimately take a while.
CALL_TIMEOUT = float(os.environ.get("VIBEUE_CALL_TIMEOUT", "600"))

# Headers that must never be copied straight through a proxy hop.
HOP_BY_HOP_HEADERS = {
    "host", "connection", "content-length", "transfer-encoding", "keep-alive",
    "proxy-authenticate", "proxy-authorization", "te", "trailers", "upgrade",
}

MCP_PROTOCOL_VERSION_FALLBACK = "2025-06-18"

# ---- Manifest + upstream probing

def load_manifest():
    """Cached tools list (list of MCP tool dicts), or [] if the manifest is missing/unreadable/malformed."""
    try:
        with open(MANIFEST_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return []
    return data if isinstance(data, list) else []

def is_upstream_reachable():
    """Cheap per-request probe. Never cached -- the editor can start or stop at any moment."""
    try:
        with socket.create_connection((UPSTREAM_HOST, UPSTREAM_PORT), timeout=PROBE_TIMEOUT):
            return True
    except OSError:
        return False

# ---- Relay mode

def relay_request(method, raw_path, client_headers, body):
    """Forward one request to the upstream VibeUE server. Returns (status, headers, body) for
    any HTTP-level answer, including upstream error statuses. Raises on connection-level
    failure so the caller can fall back to offline mode instead of surfacing a proxy error."""
    url = "http://{}:{}{}".format(UPSTREAM_HOST, UPSTREAM_PORT, raw_path)
    fwd_headers = {}
    for key, value in client_headers.items():
        if key.lower() not in HOP_BY_HOP_HEADERS:
            fwd_headers[key] = value
    fwd_headers["X-VibeUE-Proxy"] = "1"

    request = urllib.request.Request(url, data=(body or None), method=method, headers=fwd_headers)
    try:
        with urllib.request.urlopen(request, timeout=CALL_TIMEOUT) as resp:
            resp_body = resp.read()
            resp_headers = [(k, v) for k, v in resp.getheaders() if k.lower() not in HOP_BY_HOP_HEADERS]
            return resp.getcode(), resp_headers, resp_body
    except urllib.error.HTTPError as e:
        # A valid MCP-level answer with an error status -- not a proxy failure.
        resp_body = e.read()
        resp_headers = [(k, v) for k, v in e.headers.items() if k.lower() not in HOP_BY_HOP_HEADERS]
        return e.code, resp_headers, resp_body

# ---- Offline JSON-RPC handling

def _is_notification(obj):
    """A JSON-RPC notification has no 'id', or targets a notifications/* method; it must never get a response."""
    if not isinstance(obj, dict):
        return False
    if "id" not in obj:
        return True
    method = obj.get("method")
    return isinstance(method, str) and method.startswith("notifications/")

def _rpc_error(req_id, code, message):
    return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}

def _handle_single_rpc(obj):
    """Answer one parsed JSON-RPC request while the editor is offline. Returns the response
    dict, or None for a notification (which must not be answered)."""
    if _is_notification(obj):
        return None
    if not isinstance(obj, dict):
        return _rpc_error(None, -32600, "Invalid Request")

    req_id = obj.get("id")
    method = obj.get("method")

    if method == "initialize":
        params = obj.get("params") or {}
        protocol_version = params.get("protocolVersion") or MCP_PROTOCOL_VERSION_FALLBACK
        result = {
            "protocolVersion": protocol_version,
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "VibeUE (proxy, editor offline)", "version": "1.0"},
        }
        return {"jsonrpc": "2.0", "id": req_id, "result": result}

    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": req_id, "result": {"tools": load_manifest()}}

    if method == "ping":
        return {"jsonrpc": "2.0", "id": req_id, "result": {}}

    if method == "tools/call":
        params = obj.get("params") or {}
        tool_name = params.get("name", "<unknown>")
        message = (
            "The Unreal Editor is not running, so the '{}' tool cannot execute right now. "
            "Its tool list is being served from a cached manifest. Start the Unreal Editor "
            "and this tool will work immediately -- no client restart needed."
        ).format(tool_name)
        result = {"content": [{"type": "text", "text": message}], "isError": True}
        return {"jsonrpc": "2.0", "id": req_id, "result": result}

    return _rpc_error(req_id, -32601, "Method not found")

# ---- Logging

def log_line(method, path, outcome):
    print("{} {} -> {}".format(method, path, outcome))
    sys.stdout.flush()

# ---- HTTP handler

class ProxyHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "VibeUEProxy/1.0"

    def log_message(self, fmt, *args):
        pass  # silenced -- we print our own single-line summary instead

    def do_GET(self):
        self._safe(self._dispatch, "GET")

    def do_POST(self):
        self._safe(self._dispatch, "POST")

    def do_DELETE(self):
        self._safe(self._dispatch, "DELETE")

    def do_OPTIONS(self):
        self._safe(self._dispatch, "OPTIONS")

    def _safe(self, fn, *args):
        """Never let one bad request take the server down."""
        try:
            fn(*args)
        except (BrokenPipeError, ConnectionResetError):
            pass  # client went away mid-response; nothing to do
        except Exception:
            traceback.print_exc()
            try:
                self._send_plain(500, "internal proxy error")
            except Exception:
                pass

    def _dispatch(self, method):
        path = urllib.parse.urlsplit(self.path).path
        if not (path == "/mcp" or path.startswith("/mcp/")):
            self._send_plain(404, "not found")
            return

        if method == "OPTIONS":
            self._send_cors_preflight()
            log_line(method, path, "offline(OPTIONS)")
            return

        body = self._read_body()
        if is_upstream_reachable():
            try:
                status, headers, resp_body = relay_request(method, self.path, self.headers, body)
            except Exception as e:
                # The editor vanished between the probe and the call, or refused the
                # request at connection level. This is logged rather than swallowed:
                # a silent fallback is indistinguishable from "editor offline" and
                # would hide a genuine relay bug behind a plausible-looking answer.
                log_line(method, path, "relay-failed({}: {}) -> offline".format(type(e).__name__, e))
            else:
                # Sent outside the try on purpose: a failure while writing the response
                # must not fall through to offline mode, or the client gets two answers.
                self._send_relayed(status, headers, resp_body)
                log_line(method, path, "relay")
                return

        self._dispatch_offline(method, path, body)

    def _dispatch_offline(self, method, path, body):
        if method == "GET":
            self._send_plain(406, "Unreal Editor is offline; no SSE stream is available.")
            log_line(method, path, "offline(GET)")
        elif method == "DELETE":
            self._send_plain(200, "Session terminated")
            log_line(method, path, "offline(DELETE)")
        elif method == "POST":
            self._dispatch_offline_post(path, body)
        else:
            self._send_plain(404, "not found")
            log_line(method, path, "offline(unsupported)")

    def _dispatch_offline_post(self, path, body):
        try:
            text = body.decode("utf-8") if body else ""
            parsed = json.loads(text) if text.strip() else None
        except (UnicodeDecodeError, json.JSONDecodeError):
            parsed = None

        if parsed is None:
            self._send_json(200, _rpc_error(None, -32700, "Parse error"))
            log_line("POST", path, "offline(parse-error)")
            return

        items = parsed if isinstance(parsed, list) else [parsed]
        methods_seen = ",".join(i.get("method", "?") if isinstance(i, dict) else "?" for i in items)
        needs_session_id = any(isinstance(i, dict) and i.get("method") == "initialize" for i in items)
        extra_headers = {"Mcp-Session-Id": uuid.uuid4().hex} if needs_session_id else None

        results = [r for r in (_handle_single_rpc(i) for i in items) if r is not None]
        if not results:
            self._send_no_body(202, extra_headers)
        else:
            self._send_json(200, results if isinstance(parsed, list) else results[0], extra_headers)
        log_line("POST", path, "offline({})".format(methods_seen))

    # -- response helpers --

    def _read_body(self):
        try:
            length = int(self.headers.get("Content-Length", 0))
        except (TypeError, ValueError):
            length = 0
        return self.rfile.read(length) if length > 0 else b""

    def _send_plain(self, status, text):
        data = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_json(self, status, payload, extra_headers=None):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _send_no_body(self, status, extra_headers=None):
        self.send_response(status)
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _send_relayed(self, status, headers, body):
        # send_response_only -- upstream's own Server/Date headers are part of
        # "headers", so send_response()'s auto Server/Date would duplicate them.
        self.send_response_only(status)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _send_cors_preflight(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Mcp-Session-Id, MCP-Protocol-Version, Accept")
        self.send_header("Content-Length", "0")
        self.end_headers()

# ---- Entry point

class ProxyServer(ThreadingHTTPServer):
    """Threading server that keeps routine client disconnects out of the log."""
    daemon_threads = True

    def handle_error(self, request, client_address):
        # Raised below the request handler (while reading the next keep-alive
        # request line), so the handler's own guard cannot catch these.
        exc = sys.exc_info()[1]
        if isinstance(exc, (BrokenPipeError, ConnectionResetError, ConnectionAbortedError)):
            return  # client hung up; routine, not an error
        traceback.print_exc()

def _manifest_status():
    if not os.path.isfile(MANIFEST_PATH):
        return "missing"
    return "present, {} tool(s)".format(len(load_manifest()))

def main():
    server = ProxyServer((LISTEN_HOST, LISTEN_PORT), ProxyHandler)
    msg = "[vibeue-proxy] listening on http://{}:{}/mcp -> upstream http://{}:{}/mcp | manifest: {} ({})"
    print(msg.format(LISTEN_HOST, LISTEN_PORT, UPSTREAM_HOST, UPSTREAM_PORT, MANIFEST_PATH, _manifest_status()))
    sys.stdout.flush()  # redirected stdout is block-buffered; surface this line immediately
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[vibeue-proxy] shutting down")
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
