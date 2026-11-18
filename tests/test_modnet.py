"""installer/modnet.c — the module manager's HTTP client and archive list.

Built for the host over retroplat's host transport and pointed at a web
server run here, so nothing leaves the machine.
"""
import http.server
import os
import socketserver
import subprocess
import threading

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
R = os.path.join(ROOT, "third_party", "retroplat")
SRC = [os.path.join(ROOT, "tests", "modnet_host.c"),
       os.path.join(ROOT, "installer", "modnet.c"),
       os.path.join(R, "backends", "host", "transport_host.c"),
       os.path.join(R, "backends", "host", "mem_host.c")]

BLOB = bytes((i * 7) & 0xFF for i in range(200_000))
EXPORT = (
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n<html>\n"
    "<head><title>Module Listing - Export</title></head>\n<body>\n<pre>"
    "pc/modules/0-9/0tocat.zip|0tocat.zip|To Catch a Thief|49|Geoffk|2-4|Average|"
    "Not Hacked|5|1993-07-27|Lead your party through the town of Dysteri.\n"
    "pc/modules/0-9/1temple.zip|1temple.zip|The Temple of Elemental Evil|1201|"
    "Dan Crawford|1|Average|Hacked|20|1996-10-23|Adapted from the original AD&amp;D "
    "module by Gary Gygax.\n"
    "pc/modules/s/sl1-rich.zip|sl1-richkidsandtheirtoys.|SL1-Rich Kids|159|J. D.|2|"
    "Poor|Hacked|3|2023-11-19|The city of Neverwinter has offered a reward.\n"
    " </pre>\n</body>\n</html>\n").encode()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *a):
        pass

    def do_GET(self):
        p = self.path
        if p == "/modulelist/export.php":
            self.reply(EXPORT)
        elif p == "/blob.zip":
            self.reply(BLOB)
        elif p == "/moved":
            self.send_response(302)
            self.send_header("Location", "/blob.zip")
            self.end_headers()
        elif p == "/chunked":
            # an HTTP/1.1-style chunked body, which some servers send anyway
            self.wfile.write(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            for i in range(0, len(BLOB), 30_000):
                part = BLOB[i:i + 30_000]
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif p == "/nolength":
            # no Content-Length: the body ends when the server closes
            self.wfile.write(b"HTTP/1.0 200 OK\r\n\r\n" + BLOB)
        elif p == "/short":
            self.wfile.write(b"HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n"
                             % len(BLOB) + BLOB[:1000])
        else:
            self.send_error(404)

    def reply(self, body):
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


@pytest.fixture(scope="module")
def server():
    socketserver.TCPServer.allow_reuse_address = True
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield "http://127.0.0.1:%d" % srv.server_address[1]
    srv.shutdown()


@pytest.fixture(scope="module")
def drv(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("modnet") / "modnet_host")
    subprocess.run(["cc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + os.path.join(R, "include"),
                    "-I" + os.path.join(ROOT, "installer"), "-o", exe] + SRC, check=True)
    return exe


def get(drv, url, out):
    r = subprocess.run([drv, "get", url, str(out)], capture_output=True, text=True,
                       timeout=60)
    return r.stdout.strip()


@pytest.mark.parametrize("path", ["/blob.zip", "/moved", "/chunked"])
def test_a_file_arrives_whole(drv, server, tmp_path, path):
    out = tmp_path / "f"
    assert get(drv, server + path, out) == "OK %d" % len(BLOB)
    assert out.read_bytes() == BLOB


def test_a_body_without_a_length_ends_where_the_server_closes(drv, server, tmp_path):
    out = tmp_path / "f"
    assert get(drv, server + "/nolength", out) == "OK %d" % len(BLOB)
    assert out.read_bytes() == BLOB


def test_failures_say_what_happened(drv, server, tmp_path):
    out = tmp_path / "f"
    assert get(drv, server + "/short", out).startswith("ERR the download was cut short")
    assert get(drv, server + "/nothere", out) == "ERR the server answered 404"
    assert get(drv, "https://example.org/x", out).startswith("ERR not a plain http://")
    assert not out.exists()


def test_the_archive_list_parses(drv, server):
    r = subprocess.run([drv, "list", server + "/modulelist/export.php"],
                       capture_output=True, text=True, timeout=60)
    lines = r.stdout.splitlines()
    assert lines[0] == "COUNT 3"
    rows = [l[2:].split("|") for l in lines[1:]]
    assert rows[0][:8] == ["http://frua.rosedragon.org/pc/modules/0-9/0tocat.zip",
                           "To Catch a Thief", "Geoffk", "49", "2-4", "Average",
                           "0", "1993-07-27"]
    assert rows[1][6] == "1" and "AD&D module" in rows[1][8]      # entity decoded
    # the archive's file column is cut short; the path is what is used
    assert rows[2][0].endswith("/pc/modules/s/sl1-rich.zip")
