#!/bin/sh
# Install the static aarch64 bundle. Does not enable or start authentication.
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run as root' >&2; exit 1; }
[ "$(uname -m)" = aarch64 ] || { echo 'This bundle requires aarch64 Linux' >&2; exit 1; }
base=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
cd "$base"
sha256sum -c payload.sha256 || exit 1
[ -f /etc/rc.common ] && [ -f /lib/functions/network.sh ] || {
	echo 'OpenWrt / LibWrt service helpers are required' >&2; exit 1;
}
for cmd in ip flock ubus uci; do
	command -v "$cmd" >/dev/null 2>&1 || { echo "Missing $cmd: install ip-full, flock and ca-bundle from your firmware feed" >&2; exit 1; }
done
ip -4 -N rule show >/dev/null 2>&1 || { echo 'Full iproute2 (ip-full) is required' >&2; exit 1; }
./rootfs/usr/bin/njupt-login --version || exit 1
if [ -e /usr/bin/njupt-login ] || [ -e /etc/init.d/njupt-login ]; then
	echo 'Existing njupt-login installation found. Stop and back it up before replacing it.' >&2
	exit 1
fi
umask 077
mkdir -p /usr/libexec/njupt-login /etc/hotplug.d/iface
cp rootfs/usr/bin/njupt-login /usr/bin/njupt-login
cp rootfs/usr/libexec/njupt-login/network /usr/libexec/njupt-login/network
cp rootfs/usr/libexec/njupt-login/openclash-bypass /usr/libexec/njupt-login/openclash-bypass
cp rootfs/etc/init.d/njupt-login /etc/init.d/njupt-login
cp rootfs/etc/hotplug.d/iface/95-njupt-login /etc/hotplug.d/iface/95-njupt-login
if [ ! -e /etc/config/njupt-login ]; then
	cp rootfs/etc/config/njupt-login /etc/config/njupt-login
fi
chmod 600 /etc/config/njupt-login
chmod 755 /usr/bin/njupt-login /usr/libexec/njupt-login/network /usr/libexec/njupt-login/openclash-bypass /etc/init.d/njupt-login
chmod 644 /etc/hotplug.d/iface/95-njupt-login
echo 'Installed. Edit /etc/config/njupt-login with your credentials and enabled=1, then:'
echo '  /etc/init.d/njupt-login enable'
echo '  /etc/init.d/njupt-login restart'
echo '  logread -e njupt-login'
