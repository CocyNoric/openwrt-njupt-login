"""End-to-end tests against a loopback portal; no campus credentials or Internet traffic."""
import contextlib
import http.server
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse

EXE = str(pathlib.Path(sys.argv.pop(1)).resolve())
USER = "student+@isp&x=尾"
PASSWORD = " p#=+&'尾 "


class Portal(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, code, body=b"", **headers):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        for key, value in headers.items():
            self.send_header(key.replace("_", "-"), value)
        self.end_headers()
        with contextlib.suppress(BrokenPipeError, ConnectionResetError):
            self.wfile.write(body)

    def do_GET(self):
        state = self.server.state
        state["gets"].append(self.path)
        if self.path == "/a79.htm":
            if state.get("redirect_page"):
                self.reply(302, Location="/stolen")
            elif state.get("bad_mac"):
                self.reply(200, b"ss1='not-a-mac';")
            else:
                self.reply(200, b"\x81\x82 var ss1 = '001122aabbcc';")
        elif self.path == "/fallback":
            self.reply(204)
        elif self.path == "/probe":
            if state.get("huge"):
                self.reply(200, b"x" * 70000)
            elif state.get("redirect_probe"):
                self.reply(302, Location="/fallback")
            elif state.get("online"):
                self.reply(200, b"known-online-token")
            else:
                self.reply(200, b"Authentication is required")
        else:
            self.reply(404)

    def do_POST(self):
        state = self.server.state
        length = int(self.headers.get("Content-Length", "0"))
        form = urllib.parse.parse_qs(self.rfile.read(length).decode(), keep_blank_values=True)
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query, keep_blank_values=True)
        state["posts"].append((query, form, self.client_address[0], self.headers.get("Content-Type")))
        if state.get("redirect_login"):
            self.reply(307, Location="/stolen")
        elif state.get("billing_reject"):
            self.reply(200, "<script>Msg=01;msga='本账号费用超支，禁止使用';</script>Login fail".encode("gbk"))
        elif state.get("code_reject"):
            self.reply(200, b"<script>Msg=01;msga='AC999';</script>Login fail")
        elif state.get("reject"):
            self.reply(200, b"Login failed")
        else:
            if not state.get("false_success"):
                state["online"] = True
            self.reply(200, b"other portal response" if state.get("no_ack") else b"Login succeed")


class Integration(unittest.TestCase):
    def setUp(self):
        self.state = {"online": False, "posts": [], "gets": []}
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Portal)
        self.server.daemon_threads = True
        self.server.state = self.state
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.temp = tempfile.TemporaryDirectory()
        self.config = pathlib.Path(self.temp.name) / "config"
        self.port = self.server.server_port

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp.cleanup()

    def command(self, extra="", mode="--once"):
        self.config.write_text(
            f"username={USER}\npassword={PASSWORD}\nportal_ip=127.0.0.1\n"
            f"page_port={self.port}\nlogin_port={self.port}\nclient_ip=127.0.0.1\n"
            f"mark=0\nsettle=0\ntimeout=2\nconnect_timeout=1\n"
            f"probe_url=http://127.0.0.1:{self.port}/probe\nprobe_body=known-online-token\n" + extra,
            encoding="utf-8",
        )
        os.chmod(self.config, 0o600)
        cmd = [EXE, "--config", str(self.config)]
        if os.name != "nt":
            cmd += ["--interface", "lo"]
        return cmd + ([mode] if mode else [])

    def run_client(self, extra="", mode="--once"):
        env = dict(os.environ, http_proxy="http://127.0.0.1:9", ALL_PROXY="http://127.0.0.1:9", no_proxy="")
        result = subprocess.run(self.command(extra, mode), capture_output=True, text=True, timeout=8, env=env)
        self.assertNotIn(PASSWORD, result.stdout + result.stderr)
        self.assertNotIn(USER, result.stdout + result.stderr)
        return result

    def test_protocol_and_credential_encoding(self):
        result = self.run_client()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.state["posts"]), 1)
        query, form, source, content_type = self.state["posts"][0]
        self.assertEqual(form, {"DDDDD": [USER], "upass": [PASSWORD]})
        self.assertEqual(query, {"c": ["ACSetting"], "a": ["Login"], "loginMethod": ["1"],
            "wlanuserip": ["127.0.0.1"], "wlanacip": ["127.0.0.1"], "wlanacname": [""],
            "mac": ["001122aabbcc"], "vlan": ["0"], "R1": ["0"], "R2": ["0"], "R3": ["0"],
            "R6": ["0"], "para": ["00"], "0MKKey": ["123456"]})
        self.assertEqual(source, "127.0.0.1")
        self.assertEqual(content_type, "application/x-www-form-urlencoded")

    def test_online_skips_login(self):
        self.state["online"] = True
        self.assertEqual(self.run_client().returncode, 0)
        self.assertFalse(self.state["posts"])

    def test_check_never_authenticates(self):
        self.assertEqual(self.run_client(mode="--check").returncode, 1)
        self.assertFalse(self.state["posts"])

    def test_validation_has_no_network_io(self):
        self.assertEqual(self.run_client(mode="--validate").returncode, 0)
        self.assertFalse(self.state["posts"])
        self.assertFalse(self.state["gets"])

    def test_success_ack_requires_connectivity(self):
        self.state["false_success"] = True
        result = self.run_client()
        self.assertEqual(result.returncode, 1)
        self.assertIn("state=backoff", result.stderr)

    def test_variant_ack_can_confirm_online(self):
        self.state["no_ack"] = True
        self.assertEqual(self.run_client().returncode, 0)

    def test_bad_mac_never_sends_credentials(self):
        self.state["bad_mac"] = True
        self.assertEqual(self.run_client().returncode, 1)
        self.assertFalse(self.state["posts"])

    def test_portal_redirect_not_followed(self):
        self.state["redirect_page"] = True
        self.assertEqual(self.run_client().returncode, 1)
        self.assertFalse(self.state["posts"])
        self.assertNotIn("/stolen", self.state["gets"])

    def test_login_redirect_not_followed(self):
        self.state["redirect_login"] = True
        self.assertEqual(self.run_client().returncode, 1)
        self.assertEqual(len(self.state["posts"]), 1)

    def test_probe_redirect_not_followed(self):
        self.state["redirect_probe"] = True
        self.assertEqual(self.run_client(mode="--check").returncode, 1)
        self.assertNotIn("/fallback", self.state["gets"])

    def test_second_probe_fallback(self):
        extra = f"probe2_url=http://127.0.0.1:{self.port}/fallback\nprobe2_status=204\n"
        self.assertEqual(self.run_client(extra).returncode, 0)
        self.assertFalse(self.state["posts"])

    def test_fixed_resolution_bypasses_fake_dns(self):
        extra = f"probe2_url=http://does-not-exist.invalid:{self.port}/fallback\nprobe2_ip=127.0.0.1\n"
        self.assertEqual(self.run_client(extra).returncode, 0)

    def test_body_limit(self):
        self.state["huge"] = True
        self.assertEqual(self.run_client(mode="--check").returncode, 1)

    def test_credential_rejection(self):
        self.state["reject"] = True
        self.assertEqual(self.run_client().returncode, 1)

    def test_billing_rejection_is_identified_and_throttled(self):
        self.state["billing_reject"] = True
        result = self.run_client()
        self.assertEqual(result.returncode, 1)
        self.assertIn("account billing limit exceeded", result.stderr)
        self.assertIn("state=account_rejected", result.stderr)
        self.assertIn("retry in 900 seconds", result.stderr)

    def test_no_unpinned_hostname(self):
        result = self.run_client("probe2_url=http://does-not-exist.invalid/fallback\n")
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.state["posts"])

    def test_server_code_is_logged_without_assuming_billing(self):
        self.state["code_reject"] = True
        result = self.run_client()
        self.assertEqual(result.returncode, 1)
        self.assertIn("portal rejected login: server code AC999", result.stderr)
        self.assertIn("state=backoff", result.stderr)
        self.assertNotIn("account_rejected", result.stderr)
        self.assertNotIn(USER, result.stderr)
        self.assertNotIn(PASSWORD, result.stderr)

    @unittest.skipIf(os.name == "nt", "POSIX SIGTERM path")
    def test_heartbeat_reauth_and_shutdown(self):
        proc = subprocess.Popen(self.command("interval=1\nfailure_threshold=3\n", mode=""),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 8
            while not self.state["online"] and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(self.state["online"])
            self.state["online"] = False
            while len(self.state["posts"]) < 2 and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertEqual(len(self.state["posts"]), 2)
            proc.send_signal(signal.SIGTERM)
            _, err = proc.communicate(timeout=3)
            self.assertEqual(proc.returncode, 0, err)
            self.assertIn("state=suspect", err)
            self.assertIn("state=stopped", err)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()


if __name__ == "__main__":
    unittest.main(verbosity=2)
