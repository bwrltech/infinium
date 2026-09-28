"""The Print Journal web page: a small local server for browsing, rating and reprinting.

Only listens on 127.0.0.1, so it is reachable from this computer only.
Prints are addressed by their folder name, which never changes (list numbers do).
"""
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse

import printjournal as pj

HERE = os.path.dirname(os.path.abspath(__file__))


def find(entry_id):
    """(folder, summary) for a folder name, or None. Rejects anything path-like."""
    if not entry_id or "/" in entry_id or entry_id.startswith("."):
        return None
    folder = os.path.join(pj.JOURNAL, entry_id)
    try:
        with open(os.path.join(folder, "summary.json"), encoding="utf-8") as f:
            return folder, json.load(f)
    except (OSError, ValueError):
        return None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # keep the service log for saved prints only
        pass

    def send(self, status, body=b"", ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(length) or b"{}")
        except ValueError:
            return {}

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            with open(os.path.join(HERE, "web", "index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html; charset=utf-8")
        if path == "/api/prints":
            items = []
            for folder, s in pj.entries():
                s = dict(s, id=os.path.basename(folder),
                         has_thumb=os.path.exists(os.path.join(folder, "plate.png")))
                items.append(s)
            return self.send(200, items)
        if path.startswith("/thumb/"):
            hit = find(unquote(path[len("/thumb/"):]))
            if not hit:
                return self.send(404, {"error": "not found"})
            try:
                with open(os.path.join(hit[0], "plate.png"), "rb") as f:
                    return self.send(200, f.read(), "image/png")
            except OSError:
                return self.send(404, {"error": "no picture"})
        return self.send(404, {"error": "not found"})

    def do_POST(self):
        path = urlparse(self.path).path
        data = self.body()
        hit = find(str(data.get("id", "")))
        if not hit:
            return self.send(404, {"error": "That print is no longer in the journal."})
        folder, s = hit

        if path == "/api/rate":
            if "rating" in data:
                if data["rating"] not in ("good", "bad", None):
                    return self.send(400, {"error": "rating must be good, bad or empty"})
                s["rating"] = data["rating"]
            if "note" in data:
                s["note"] = str(data["note"])[:2000]
            pj.save_summary(folder, s)
            return self.send(200, s)

        if path == "/api/reprint":
            try:
                pj.open_in_infinium(folder)
            except FileNotFoundError as e:
                return self.send(500, {"error": str(e)})
            return self.send(200, {"ok": True})

        if path == "/api/preset":
            try:
                return self.send(200, pj.make_presets_for(folder, s, str(data.get("name", ""))))
            except ValueError as e:
                return self.send(400, {"error": str(e)})

        return self.send(404, {"error": "not found"})


def serve(port):
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
