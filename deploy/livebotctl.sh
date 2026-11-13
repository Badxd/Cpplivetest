#!/usr/bin/env bash
# Start/stop/status helper. Usage: livebotctl.sh {check-env|start|stop|restart|status|logs|results|health}
# It never prints environment values or secrets. check-env reports names and problems only.
set -euo pipefail
ENV_FILE="${LIVEBOT_ENV_FILE:-/etc/cppgram-livebot.env}"
LOG_DIR=/var/log/cppgram-livebot
UNIT=cppgram-livebot.service

check_env() {
    if [[ ! -f "$ENV_FILE" ]]; then echo "missing: $ENV_FILE" >&2; return 1; fi
    local mode owner
    mode="$(stat -c '%a' "$ENV_FILE")"
    owner="$(stat -c '%U:%G' "$ENV_FILE")"
    if [[ "$mode" != "600" ]]; then echo "bad permissions on $ENV_FILE: $mode (need 600)" >&2; return 1; fi
    if [[ "$owner" != "root:root" ]]; then echo "bad owner on $ENV_FILE: $owner (need root:root)" >&2; return 1; fi
    local bad=0
    for name in TG_API_ID TG_API_HASH TG_BOT_TOKEN TG_ADMIN_IDS; do
        if ! grep -qE "^${name}=." "$ENV_FILE"; then echo "not set: $name" >&2; bad=1; fi
        if grep -qE "^${name}=CHANGE_ME" "$ENV_FILE"; then echo "still a placeholder: $name" >&2; bad=1; fi
    done
    for prefix in PROXY_PLAIN PROXY_DD; do
        local set_count
        set_count="$(grep -cE "^TG_${prefix}_(HOST|PORT|SECRET)=." "$ENV_FILE" || true)"
        if [[ "$set_count" != "0" && "$set_count" != "3" ]]; then
            echo "TG_${prefix}_* must be all set or all unset" >&2; bad=1
        fi
    done
    if [[ $bad -ne 0 ]]; then return 1; fi
    echo "env file looks complete (values not shown)"
}

case "${1:-}" in
    check-env) check_env ;;
    start)     check_env && systemctl start "$UNIT" && echo "started" ;;
    stop)      systemctl stop "$UNIT" && echo "stopped" ;;
    restart)   check_env && systemctl restart "$UNIT" && echo "restarted" ;;
    status)    systemctl --no-pager status "$UNIT" || true ;;
    logs)      journalctl -u "$UNIT" -n 200 --no-pager ;;
    results)   tail -n 50 "$LOG_DIR/results.jsonl" 2>/dev/null || echo "no results yet" ;;
    health)    systemctl is-active "$UNIT" && systemctl show "$UNIT" -p MainPID,MemoryCurrent,CPUUsageNSec --no-pager ;;
    *) echo "usage: $0 {check-env|start|stop|restart|status|logs|results|health}" >&2; exit 2 ;;
esac
