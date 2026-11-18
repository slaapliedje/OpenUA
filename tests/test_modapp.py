"""installer/modapp.c — the module manager's screen and actions, headless.

The shell is tests/modapp_host.c over retroplat's host backend, which keeps
the text it was asked to draw; the archive is a web server run here.
"""
import functools
import http.server
import io
import os
import socketserver
import subprocess
import threading
import zipfile

import pytest

ROOT = os.path.join(os.path.dirname(__file__), "..")
R = os.path.join(ROOT, "third_party", "retroplat")
SRC = ([os.path.join(ROOT, "tests", "modapp_host.c")]
       + [os.path.join(ROOT, "installer", f) for f in
          ("modapp.c", "modnet.c", "modinst.c", "modcore.c", "miniz.c")]
       + [os.path.join(ROOT, "src", "convert", f) for f in ("artconv.c", "xmi2slb.c")]
       + [os.path.join(R, "backends", "host", f + ".c") for f in
          ("transport_host", "mem_host", "draw_host", "metrics_host")]
       + [os.path.join(R, "src", f + ".c") for f in
          ("utf8", "papermetrics", "papermetrics_tables", "fontpick", "monobits",
           "polyfill")])

GAME001 = b"THE TEST MODULE" + bytes(388 - 15)


def module_zip():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Testmod.dsn/GAME001.DAT", GAME001)
        z.writestr("Testmod.dsn/GEO005.DAT", b"g" * 1000)
        z.writestr("Testmod.dsn/RESIZEPC.EXE", b"MZ")
    return buf.getvalue()


ZIP = module_zip()
EXPORT = ("<html><body><pre>"
          "pc/modules/a/alpha.zip|alpha.zip|Alpha Quest|10|Ann|1-2|Average|Not Hacked|1|"
          "1994-01-01|The first one.\n"
          "pc/modules/t/testmod.zip|testmod.zip|Test Module|1|Tess|3|+1|Hacked|2|"
          "1995-02-02|A module made by the test suite to be installed.\n"
          "pc/modules/g/gone.zip|gone.zip|Gone Module|1|Gil|3|Poor|Not Hacked|2|"
          "1995-02-02|Its file is missing on the server.\n"
          " </pre></body></html>\n").encode()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        body = {"/modulelist/export.php": EXPORT, "/pc/modules/t/testmod.zip": ZIP,
                "/pc/modules/a/alpha.zip": b"<!DOCTYPE HTML><html>moved</html>"
                }.get(self.path)
        if body is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


@pytest.fixture(scope="module")
def server():
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield "http://127.0.0.1:%d" % srv.server_address[1]
    srv.shutdown()


@pytest.fixture(scope="module")
def drv(tmp_path_factory):
    exe = str(tmp_path_factory.mktemp("modapp") / "modapp_host")
    inc = ["-I" + os.path.join(R, "include"), "-I" + os.path.join(R, "backends", "host"),
           "-I" + os.path.join(R, "src"), "-I" + os.path.join(ROOT, "installer")]
    subprocess.run(["cc", "-std=gnu99", "-O2", "-w"] + inc + ["-o", exe] + SRC, check=True)
    return exe


def run(drv, folder, server, *keys):
    r = subprocess.run([drv, str(folder), server + "/modulelist/export.php",
                        server + "/"] + list(keys), capture_output=True, text=True,
                       timeout=120)
    screens, cur = [], {"text": []}
    for line in r.stdout.splitlines():
        if line.startswith("STATUS "):
            cur["status"] = line[7:]
        elif line.startswith("SEL "):
            cur["sel"] = int(line[4:])
        elif line.startswith("T "):
            cur["text"].append(line[2:])
        elif line == "--":
            screens.append(cur)
            cur = {"text": []}
    return screens


@pytest.fixture
def folder(tmp_path):
    (tmp_path / "FRUA.PRG").write_bytes(b"")
    return tmp_path


def test_first_start_asks_for_the_list(drv, server, folder):
    s = run(drv, folder, server)
    assert "press R" in s[0]["status"]


def test_the_list_is_fetched_kept_and_browsed(drv, server, folder):
    s = run(drv, folder, server, "r", "down", "g", "up")
    assert s[1]["status"] == "3 modules in the archive's list."
    assert any(t.startswith("1 Alpha Quest") for t in s[1]["text"])
    assert [x["sel"] for x in s[1:]] == [0, 1, 2, 1]
    assert (folder / "MODLIST.TXT").exists()
    # next time it is there without the network
    s = run(drv, folder, "http://127.0.0.1:9")
    assert s[0]["status"].startswith("3 modules")


def test_install_downloads_installs_and_marks_it(drv, server, folder):
    s = run(drv, folder, server, "r", "down", "enter")
    assert s[-1]["status"] == "Test Module is installed as TESTMOD.DSN."
    d = folder / "TESTMOD.DSN"
    assert (d / "GAME001.DAT").read_bytes() == GAME001
    assert not (d / "RESIZEPC.EXE").exists()
    assert not (folder / "MODDL.ZIP").exists()
    assert "pc/modules/t/testmod.zip|TESTMOD.DSN" in (folder / "MODINST.TXT").read_text()
    # the list row carries H (replaces art) and I (installed); the host
    # backend keeps only the first 47 characters of a drawn string
    assert any(t.startswith("2 Tess") and t.endswith("HI") for t in s[-1]["text"])
    assert any(t.startswith("0 OpenUA Module Manager - 3 modules, 1 installed")
               for t in s[-1]["text"])


@pytest.mark.parametrize("pick,why", [(0, "web page"), (2, "answered 404")])
def test_a_bad_download_says_why_and_installs_nothing(drv, server, folder, pick, why):
    s = run(drv, folder, server, "r", *(["down"] * pick), "enter")
    assert why in s[-1]["status"]
    assert sorted(p.name for p in folder.iterdir()) == ["FRUA.PRG", "MODLIST.TXT"]
