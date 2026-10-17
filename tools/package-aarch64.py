"""Verify the cross-built ELF and create an explicit-file, mode-preserving router bundle."""
import hashlib
import json
import pathlib
import shutil
import struct
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY = ROOT / "build-aarch64/njupt-login"
VERSION = "0.1.0"
BUNDLE_RELEASE = 4


def inspect_elf(data):
    if data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("Expected ELF64 little endian")
    fields = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    kind, machine = fields[0:2]
    phoff, phsize, phnum = fields[4], fields[8], fields[9]
    if machine != 183 or kind not in (2, 3):
        raise ValueError("Expected aarch64 executable")
    if phsize != 56 or phoff + phsize * phnum > len(data):
        raise ValueError("Invalid ELF program headers")
    for i in range(phnum):
        values = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
        ptype, _, offset, _, _, size, _, _ = values
        if ptype == 3:
            raise ValueError("Unexpected dynamic interpreter")
        if ptype == 2:
            for at in range(offset, offset + size, 16):
                tag, _ = struct.unpack_from("<QQ", data, at)
                if tag == 1:
                    raise ValueError("Unexpected DT_NEEDED dependency")
    return {"format": "ELF64 little endian", "machine": "aarch64", "static": True,
            "size_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    metadata = inspect_elf(BINARY.read_bytes())
    name = f"openwrt-njupt-login-{VERSION}-r{BUNDLE_RELEASE}-aarch64-static"
    stage = ROOT / "dist" / name
    stage.mkdir(parents=True, exist_ok=True)
    payload = {
        "rootfs/usr/bin/njupt-login": (BINARY, 0o755),
        "rootfs/usr/libexec/njupt-login/network": (ROOT / "openwrt/files/network.sh", 0o755),
        "rootfs/usr/libexec/njupt-login/openclash-bypass": (ROOT / "openwrt/files/openclash-bypass.sh", 0o755),
        "rootfs/etc/init.d/njupt-login": (ROOT / "openwrt/files/njupt-login.init", 0o755),
        "rootfs/etc/hotplug.d/iface/95-njupt-login": (ROOT / "openwrt/files/njupt-login.hotplug", 0o644),
        "rootfs/etc/config/njupt-login": (ROOT / "openwrt/files/njupt-login.config", 0o600),
    }
    docs = {
        "install.sh": (ROOT / "tools/install-router.sh", 0o755),
        "README.md": (ROOT / "README.md", 0o644),
        "docs/usage.md": (ROOT / "docs/usage.md", 0o644),
        "docs/development.md": (ROOT / "docs/development.md", 0o644),
        "dependencies.json": (ROOT / "tools/dependencies.json", 0o644),
        "tools/dependencies.json": (ROOT / "tools/dependencies.json", 0o644),
        "examples/client.conf": (ROOT / "examples/client.conf", 0o644),
        "openwrt/files/njupt-login.config": (ROOT / "openwrt/files/njupt-login.config", 0o644),
        "LICENSE": (ROOT / "LICENSE", 0o644),
        "licenses/curl.txt": (ROOT / ".deps/curl-8.22.0/COPYING", 0o644),
        "licenses/mbedtls.txt": (ROOT / ".deps/mbedtls-3.6.7/LICENSE", 0o644),
        "licenses/zig.txt": (ROOT / ".deps/zig-x86_64-windows-0.15.2/LICENSE", 0o644),
        "licenses/musl.txt": (ROOT / ".deps/zig-x86_64-windows-0.15.2/lib/libc/musl/COPYRIGHT", 0o644),
        "licenses/libcxx.txt": (ROOT / ".deps/zig-x86_64-windows-0.15.2/lib/libcxx/LICENSE.TXT", 0o644),
        "licenses/libcxxabi.txt": (ROOT / ".deps/zig-x86_64-windows-0.15.2/lib/libcxxabi/LICENSE.TXT", 0o644),
        "licenses/libunwind.txt": (ROOT / ".deps/zig-x86_64-windows-0.15.2/lib/libunwind/LICENSE.TXT", 0o644),
    }
    files = {**payload, **docs}
    for dest, (source, _) in files.items():
        target = stage / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    sums = "".join(f"{hashlib.sha256((stage / p).read_bytes()).hexdigest()}  {p}\n" for p in payload)
    (stage / "payload.sha256").write_text(sums, encoding="ascii", newline="\n")
    metadata.update({"project": "openwrt-njupt-login", "license": "Apache-2.0",
        "version": VERSION, "bundle_release": BUNDLE_RELEASE, "target": "aarch64-linux-musl (ARMv8 baseline)",
        "curl": "8.22.0",
        "tls": "Mbed TLS 3.6.7", "compiler": "Zig 0.15.2 (Clang 20.1.2)",
        "package_format": "manual tar.gz bundle; not ipk/apk"})
    (stage / "build-info.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n")
    files.update({"payload.sha256": (stage / "payload.sha256", 0o644),
                  "build-info.json": (stage / "build-info.json", 0o644)})
    archive = ROOT / "dist" / f"{name}.tar.gz"
    with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as out:
        for rel, (_, mode) in sorted(files.items()):
            path = stage / rel
            info = out.gettarinfo(str(path), arcname=f"{name}/{rel}")
            info.mode, info.uid, info.gid = mode, 0, 0
            info.uname = info.gname = "root"
            with path.open("rb") as stream:
                out.addfile(info, stream)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + ".sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii", newline="\n")
    print(json.dumps({"archive": str(archive), "archive_sha256": digest, **metadata}, indent=2))


if __name__ == "__main__":
    main()
