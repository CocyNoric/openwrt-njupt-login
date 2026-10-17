#!/bin/sh
# Called by our network helper every cycle; may also be called by OpenClash's custom hook.
# Scope: only locally generated sockets bearing our exact SO_MARK value.
mark=0x4e4a
tag=njupt-login-bypass
mkdir -p /var/lock || exit 1
exec 8>/var/lock/njupt-login-openclash.lock
lock_wait=0
while ! flock -n 8; do
	[ "$lock_wait" -lt 5 ] || { logger -t njupt-login 'OpenClash lock timed out'; exit 1; }
	lock_wait=$((lock_wait + 1))
	sleep 1
done
action=${1:-setup}
case "$action" in setup|cleanup) ;; *) exit 2;; esac
failed=0
if command -v nft >/dev/null 2>&1 && nft list table inet fw4 >/dev/null 2>&1; then
	for chain in openclash_output openclash_mangle_output; do
		rules=$(nft -a list chain inet fw4 "$chain" 2>/dev/null) || continue
		if [ "$action" = setup ]; then
			printf '%s\n' "$rules" | grep -Fq "\"$tag\"" && continue
			nft insert rule inet fw4 "$chain" meta mark "$mark" counter return comment "\"$tag\"" || failed=1
		else
			printf '%s\n' "$rules" | awk -v tag="$tag" 'index($0,"\"" tag "\"") {print $NF}' | while read -r handle; do
				case "$handle" in ''|*[!0-9]*) continue;; esac
				nft delete rule inet fw4 "$chain" handle "$handle"
			done
		fi
	done
fi
# Some fw4 installations still run OpenClash through iptables compatibility chains.
if command -v iptables >/dev/null 2>&1; then
	for table in nat mangle; do
		iptables -w 2 -t "$table" -S openclash_output >/dev/null 2>&1 || continue
		if [ "$action" = setup ]; then
			iptables -w 2 -t "$table" -C openclash_output -m mark --mark "$mark/0xffffffff" -m comment --comment "$tag" -j RETURN 2>/dev/null && continue
			iptables -w 2 -t "$table" -I openclash_output 1 -m mark --mark "$mark/0xffffffff" -m comment --comment "$tag" -j RETURN || failed=1
		else
			while iptables -w 2 -t "$table" -C openclash_output -m mark --mark "$mark/0xffffffff" -m comment --comment "$tag" -j RETURN 2>/dev/null; do
				iptables -w 2 -t "$table" -D openclash_output -m mark --mark "$mark/0xffffffff" -m comment --comment "$tag" -j RETURN || break
			done
		fi
	done
fi
exit "$failed"
