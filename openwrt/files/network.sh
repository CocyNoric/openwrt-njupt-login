#!/bin/sh
# Reserved identifiers: table 20260, priority 100, fwmark 20042 (0x4e4a), route protocol 186.
# No default routes, nft base chains, or LAN forwarding rules are changed.
set -f
mkdir -p /var/lock || exit 1
exec 9>/var/lock/njupt-login-network.lock
# BusyBox flock supports -n, but not util-linux's -w timeout option.
lock_wait=0
while ! flock -n 9; do
	[ "$lock_wait" -lt 5 ] || { logger -t njupt-login 'network lock timed out'; exit 1; }
	lock_wait=$((lock_wait + 1))
	sleep 1
done
action=${1:-setup}
table=20260
priority=100
mark=0x4e4a
proto=186
owned_rule() {
	ip -4 -N rule show | awk '$1=="100:" && $2=="from" && $3=="all" && $4=="fwmark" && ($5=="0x4e4a" || $5=="0x4e4a/0xffffffff") && $6=="lookup" && $7=="20260" && NF==7 {found=1} END {exit !found}'
}
case "$action" in
cleanup)
	# Never delete a different user's rule at the same priority.
	while owned_rule; do ip -4 rule del priority "$priority" fwmark "$mark/0xffffffff" table "$table" || break; done
	ip -4 route flush table "$table" proto "$proto" 2>/dev/null
	/usr/libexec/njupt-login/openclash-bypass cleanup
	exit 0
	;;
setup) ;;
*) exit 2;;
esac
. /lib/functions.sh
. /lib/functions/network.sh
config_load njupt-login
config_get network main network wan
case "$network" in ''|*[!a-zA-Z0-9_]*) exit 1;; esac
network_flush_cache
network_get_device device "$network" || exit 1
network_get_ipaddr client_ip "$network" || exit 1
case "$device" in ''|*[!a-zA-Z0-9_.:-]*) exit 1;; esac
case "$client_ip" in ''|*[!0-9.]*) exit 1;; esac

# Refuse to reuse another service's table / priority. Route protocol is our ownership marker.
rules=$(ip -4 -N rule show) || exit 1
if printf '%s\n' "$rules" | awk '$1=="100:" {found=1} END {exit !found}'; then
	owned_rule || { logger -t njupt-login 'priority 100 is occupied; refusing route changes'; exit 1; }
	# Also reject a second rule at priority 100 even if one is ours.
	[ "$(printf '%s\n' "$rules" | awk '$1=="100:" {n++} END {print n+0}')" = 1 ] || exit 1
fi
# Read one unfiltered numeric dump. iproute2 hides "proto" when filtering by
# protocol, so comparing filtered/unfiltered display strings misidentifies our
# own routes as foreign. Dumping all tables also works before ours exists.
routes=$(ip -4 -N -o route show table all) || {
	logger -t njupt-login 'cannot inspect IPv4 routes; refusing route changes'
	exit 1
}
printf '%s\n' "$routes" | awk -v wanted_table="$table" -v wanted_proto="$proto" '
NF {
	route_table = 0; route_proto = ""
	for (i = 1; i < NF; i++) {
		if ($i == "table") route_table = $(i + 1)
		if ($i == "proto") route_proto = $(i + 1)
	}
	if (route_table == wanted_table && route_proto != wanted_proto) foreign = 1
}
END { exit foreign ? 1 : 0 }
' || { logger -t njupt-login 'table 20260 is occupied; refusing route changes'; exit 1; }

# Copy only WAN connected routes, then use the gateway reported by netifd (never the VPN default).
connected=$(ip -4 route show table main dev "$device" scope link) || exit 1
printf '%s\n' "$connected" | while read -r prefix rest; do
	case "$prefix" in ''|default|*[!0-9./]*) continue;; esac
	ip -4 route replace table "$table" "$prefix" dev "$device" src "$client_ip" scope link proto "$proto" || exit 1
done || exit 1
gateway=
network_get_gateway gateway "$network" 1
if [ -n "$gateway" ]; then
	case "$gateway" in *[!0-9.]*) exit 1;; esac
	ip -4 route replace table "$table" "$gateway/32" dev "$device" scope link proto "$proto" || exit 1
	ip -4 route replace table "$table" default via "$gateway" dev "$device" src "$client_ip" proto "$proto" || exit 1
else
	# A point-to-point interface may legitimately have a device-only default route.
	ip -4 route show table main dev "$device" | grep -q '^default dev ' || exit 1
	ip -4 route replace table "$table" default dev "$device" src "$client_ip" proto "$proto" || exit 1
fi
owned_rule || ip -4 rule add priority "$priority" fwmark "$mark/0xffffffff" table "$table" || exit 1
/usr/libexec/njupt-login/openclash-bypass setup
