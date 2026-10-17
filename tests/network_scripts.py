"""Exercise real shell helper logic with mocked ip/nft/netifd. Never changes host networking."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
def shell_path(path):
    path = pathlib.Path(path)
    return "/" + path.drive[0].lower() + path.as_posix()[2:] if os.name == "nt" else path.as_posix()

BASH = os.environ.get("NJUPT_TEST_SHELL") or shutil.which("bash")
if not BASH and os.name == "nt":
    candidate = pathlib.Path("C:/Program Files/Git/usr/bin/bash.exe")
    if candidate.exists():
        BASH = str(candidate)

MOCK = r'''
mkdir() { [ "$1" = -p ] && [ -d "$2" ]; }
config_load() { :; }
config_get() { eval "$1=wan"; }
network_flush_cache() { :; }
network_get_device() { eval "$1=eth1"; }
network_get_ipaddr() { eval "$1=10.166.33.72"; }
network_get_gateway() { eval "$1=10.166.0.1"; }
flock() {
    printf 'flock %s\n' "$*" >> "$NJUPT_TEST_DIR/log"
    # Match BusyBox's available options, rather than accepting every flag.
    [ "$#" = 2 ] && [ "$1" = -n ] || { echo 'unrecognized option' >&2; return 64; }
    case "$2" in 8|9) ;; *) return 64;; esac
    [ ! -f "$NJUPT_TEST_DIR/lock-busy-$2" ] || return 1
    if [ -f "$NJUPT_TEST_DIR/lock-once-$2" ]; then
        rm -f "$NJUPT_TEST_DIR/lock-once-$2"
        return 1
    fi
}
sleep() {
    [ "$#" = 1 ] && [ "$1" = 1 ] || return 64
    printf 'sleep %s\n' "$*" >> "$NJUPT_TEST_DIR/log"
}
logger() { printf 'logger %s\n' "$*" >> "$NJUPT_TEST_DIR/log"; }
ip() {
    printf 'ip %s\n' "$*" >> "$NJUPT_TEST_DIR/log"
    case "$*" in
    '-4 -N rule show') cat "$NJUPT_TEST_DIR/rules";;
    '-4 rule add '*) printf '100: from all fwmark 0x4e4a lookup 20260\n' >> "$NJUPT_TEST_DIR/rules";;
    '-4 rule del '*) sed '/100: from all fwmark 0x4e4a lookup 20260/d' "$NJUPT_TEST_DIR/rules" > "$NJUPT_TEST_DIR/rules.new"; mv "$NJUPT_TEST_DIR/rules.new" "$NJUPT_TEST_DIR/rules";;
    # iproute2 names protocol 186 "bgp" unless -N is used. A protocol filter
    # suppresses the protocol field entirely, so these dumps cannot be compared.
    '-4 route show table 20260') sed 's/proto 186/proto bgp/g' "$NJUPT_TEST_DIR/routes";;
    '-4 route show table 20260 proto 186') awk '/proto 186/ {sub(/ proto 186/, ""); print}' "$NJUPT_TEST_DIR/routes";;
    '-4 -N -o route show table all')
        [ ! -f "$NJUPT_TEST_DIR/fail-route-dump" ] || return 1
        awk 'NF {print $0, "table 20260"}' "$NJUPT_TEST_DIR/routes"
        cat "$NJUPT_TEST_DIR/other-routes";;
    '-4 route show table main dev eth1 scope link') printf '10.166.0.0/16 dev eth1 proto kernel scope link src 10.166.33.72\n';;
    '-4 route replace '*)
        [ ! -f "$NJUPT_TEST_DIR/fail-route" ] || return 1
        awk -v prefix="$6" '$1 != prefix' "$NJUPT_TEST_DIR/routes" > "$NJUPT_TEST_DIR/routes.new"
        shift 5
        printf '%s\n' "$*" >> "$NJUPT_TEST_DIR/routes.new"
        mv "$NJUPT_TEST_DIR/routes.new" "$NJUPT_TEST_DIR/routes";;
    '-4 route flush table 20260 proto 186') grep -v 'proto 186' "$NJUPT_TEST_DIR/routes" > "$NJUPT_TEST_DIR/routes.new" || :; mv "$NJUPT_TEST_DIR/routes.new" "$NJUPT_TEST_DIR/routes";;
    *) return 1;;
    esac
}
nft() {
    printf 'nft %s\n' "$*" >> "$NJUPT_TEST_DIR/log"
    case "$*" in
    'list table inet fw4') return 0;;
    '-a list chain inet fw4 '*)
        chain=$6
        [ ! -f "$NJUPT_TEST_DIR/no-chains" ] || return 1
        [ ! -f "$NJUPT_TEST_DIR/$chain" ] || cat "$NJUPT_TEST_DIR/$chain"
        return 0;;
    'insert rule inet fw4 '*)
        [ ! -f "$NJUPT_TEST_DIR/fail-nft" ] || return 1
        printf 'meta mark 0x4e4a counter return comment "njupt-login-bypass" # handle 7\n' > "$NJUPT_TEST_DIR/$5";;
    'delete rule inet fw4 '*) rm -f "$NJUPT_TEST_DIR/$5";;
    *) return 1;;
    esac
}
iptables() {
    [ -f "$NJUPT_TEST_DIR/use-iptables" ] || return 1
    printf 'iptables %s\n' "$*" >> "$NJUPT_TEST_DIR/log"
    case "$5" in
    -S) return 0;;
    -C) [ -f "$NJUPT_TEST_DIR/iptables-$4" ];;
    -I) [ ! -f "$NJUPT_TEST_DIR/fail-iptables" ] || return 1; printf 'owned\n' > "$NJUPT_TEST_DIR/iptables-$4";;
    -D) rm -f "$NJUPT_TEST_DIR/iptables-$4";;
    *) return 1;;
    esac
}
'''


@unittest.skipUnless(BASH, "POSIX shell unavailable")
class NetworkHelpers(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build-test-tmp"
        scratch.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch)
        self.path = pathlib.Path(self.temp.name)
        for name in ["rules", "routes", "other-routes", "log"]:
            (self.path / name).write_text("", newline="\n")
        self.env = dict(os.environ, NJUPT_TEST_DIR=shell_path(self.path), MSYS_NO_PATHCONV="1")
        mock = self.path / "mock.sh"
        mock.write_text(MOCK, encoding="utf-8", newline="\n")
        for source, target in [("openclash-bypass.sh", "bypass.sh"), ("network.sh", "network.sh")]:
            script = (ROOT / "openwrt/files" / source).read_text()
            script = script.replace(". /lib/functions.sh", ":").replace(". /lib/functions/network.sh", ":")
            script = script.replace("/var/lock", shell_path(self.path))
            script = script.replace("/usr/libexec/njupt-login/openclash-bypass", f'bash "{shell_path(self.path)}/bypass.sh"')
            (self.path / target).write_text(f'. "{shell_path(mock)}"\n' + script, encoding="utf-8", newline="\n")

    def tearDown(self):
        self.temp.cleanup()

    def run_helper(self, script="network.sh", action="setup"):
        return subprocess.run([BASH, "-c", 'export PATH="/usr/bin:/bin:$PATH"; exec bash "$@"',
                               "test", shell_path(self.path / script), action], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", env=self.env, timeout=5)

    def log(self):
        return (self.path / "log").read_text()

    def test_wan_route_and_both_openclash_chains(self):
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        log = self.log()
        self.assertIn("default via 10.166.0.1 dev eth1 src 10.166.33.72 proto 186", log)
        self.assertIn("fwmark 0x4e4a/0xffffffff table 20260", log)
        for chain in ["openclash_output", "openclash_mangle_output"]:
            self.assertIn(f"insert rule inet fw4 {chain} meta mark 0x4e4a", log)
        self.assertNotIn("route flush", log)
        self.assertNotIn("rule del", log)

    def test_setup_is_idempotent(self):
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(self.log().count("ip -4 rule add "), 1)
        self.assertEqual(self.log().count("nft insert rule"), 2)

    def test_foreign_priority_is_untouched(self):
        (self.path / "rules").write_text("100: from all lookup 999\n", newline="\n")
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertNotIn("route replace", self.log())
        self.assertNotIn("rule del", self.log())

    def test_foreign_table_is_untouched(self):
        (self.path / "routes").write_text("default dev wg0 proto static\n", newline="\n")
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertNotIn("route replace", self.log())
        self.assertNotIn("route flush", self.log())

    def test_existing_owned_routes_do_not_block_setup(self):
        (self.path / "routes").write_text(
            "default via 10.166.0.1 dev eth1 proto 186 src 10.166.33.72\n"
            "10.166.0.0/16 dev eth1 proto 186 scope link src 10.166.33.72\n",
            newline="\n")
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr + self.log())
        self.assertNotIn("is occupied", self.log())

    def test_mixed_owned_and_foreign_routes_are_untouched(self):
        routes = "default dev eth1 proto 186\n10.0.0.0/8 dev wg0 proto static\n"
        (self.path / "routes").write_text(routes, newline="\n")
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertEqual((self.path / "routes").read_text(), routes)
        self.assertNotIn("route replace", self.log())
        self.assertNotIn("route flush", self.log())

    def test_other_tables_do_not_count_as_conflicts(self):
        (self.path / "other-routes").write_text(
            "default dev wg0 table 100 proto 3\n"
            "local 127.0.0.1 dev lo table 255 proto 2\n"
            "192.168.2.0/24 dev br-lan proto 2 scope link src 192.168.2.1\n",
            newline="\n")
        self.assertEqual(self.run_helper().returncode, 0)

    def test_unlabelled_route_in_our_table_is_foreign(self):
        (self.path / "routes").write_text("default dev wg0\n", newline="\n")
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertNotIn("route replace", self.log())

    def test_route_dump_failure_prevents_route_changes(self):
        (self.path / "fail-route-dump").touch()
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertNotIn("route replace", self.log())
        self.assertNotIn("rule add", self.log())
        self.assertNotIn("route flush", self.log())

    def test_cleanup_preserves_foreign_rules_and_routes(self):
        (self.path / "rules").write_text("100: from all fwmark 0x4e4a lookup 20260\n100: from all lookup 999\n", newline="\n")
        (self.path / "routes").write_text("default dev eth1 proto 186\n10.0.0.0/8 dev wg0 proto static\n", newline="\n")
        self.assertEqual(self.run_helper(action="cleanup").returncode, 0)
        self.assertEqual((self.path / "rules").read_text(), "100: from all lookup 999\n")
        self.assertEqual((self.path / "routes").read_text(), "10.0.0.0/8 dev wg0 proto static\n")
        self.assertNotIn("rule del priority 100 table", self.log())

    def test_route_failure_stops_auth_setup(self):
        (self.path / "fail-route").touch()
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertNotIn("rule add", self.log())

    def test_firewall_failure_is_reported(self):
        (self.path / "fail-nft").touch()
        self.assertEqual(self.run_helper(script="bypass.sh").returncode, 1)

    def test_openclash_off_is_valid(self):
        (self.path / "no-chains").touch()
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertNotIn("nft insert rule", self.log())

    def test_openclash_restart_restores_exemptions(self):
        self.assertEqual(self.run_helper().returncode, 0)
        for chain in ["openclash_output", "openclash_mangle_output"]:
            (self.path / chain).unlink()
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(self.log().count("nft insert rule"), 4)

    def test_iptables_fallback_on_fw4(self):
        (self.path / "no-chains").touch()
        (self.path / "use-iptables").touch()
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(self.run_helper().returncode, 0)
        self.assertEqual(self.log().count("-I openclash_output"), 2)
        self.assertIn("--mark 0x4e4a/0xffffffff", self.log())
        self.assertEqual(self.run_helper(action="cleanup").returncode, 0)
        self.assertFalse((self.path / "iptables-nat").exists())
        self.assertFalse((self.path / "iptables-mangle").exists())

    def test_iptables_insert_failure(self):
        (self.path / "no-chains").touch()
        (self.path / "use-iptables").touch()
        (self.path / "fail-iptables").touch()
        self.assertEqual(self.run_helper(script="bypass.sh").returncode, 1)

    def test_busybox_flock_options(self):
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('flock -n 9', self.log())
        self.assertIn('flock -n 8', self.log())
        self.assertNotIn('flock -w', self.log())

    def test_network_lock_contention_recovers(self):
        (self.path / 'lock-once-9').touch()
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log().count('flock -n 9'), 2)
        self.assertEqual(self.log().count('sleep 1'), 1)

    def test_network_lock_timeout_does_not_change_routes(self):
        (self.path / 'lock-busy-9').touch()
        self.assertEqual(self.run_helper().returncode, 1)
        self.assertEqual(self.log().count('flock -n 9'), 6)
        self.assertEqual(self.log().count('sleep 1'), 5)
        self.assertNotIn('route replace', self.log())
        self.assertIn('network lock timed out', self.log())

    def test_openclash_lock_contention_recovers(self):
        (self.path / 'lock-once-8').touch()
        result = self.run_helper(script='bypass.sh')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log().count('flock -n 8'), 2)
        self.assertEqual(self.log().count('sleep 1'), 1)

    def test_openclash_lock_timeout_does_not_change_firewall(self):
        (self.path / 'lock-busy-8').touch()
        self.assertEqual(self.run_helper(script='bypass.sh').returncode, 1)
        self.assertEqual(self.log().count('flock -n 8'), 6)
        self.assertEqual(self.log().count('sleep 1'), 5)
        self.assertNotIn('nft insert rule', self.log())
        self.assertIn('OpenClash lock timed out', self.log())


if __name__ == "__main__":
    unittest.main(verbosity=2)
