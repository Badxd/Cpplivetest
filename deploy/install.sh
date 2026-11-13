#!/usr/bin/env bash
# Installs the cppgram live-test bot on Ubuntu 24.04. Run as root:
#   sudo ./deploy/install.sh /path/to/build/cppgram_live_test_bot
#
# What it does: creates the service user and the four directories, installs the binary and unit file, creates
# /etc/cppgram-livebot.env from the example if it does not exist yet, and enables the service at boot.
# It does NOT start the bot (you fill in the env file first), does NOT install packages, and does NOT touch
# any other service or directory (in particular nothing under /opt/MTProxy).
set -euo pipefail

if [[ $EUID -ne 0 ]]; then echo "run as root (sudo)" >&2; exit 1; fi
BIN_SRC="${1:-}"
if [[ -z "$BIN_SRC" || ! -x "$BIN_SRC" ]]; then echo "usage: $0 /path/to/cppgram_live_test_bot" >&2; exit 1; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

APP=/opt/cppgram-livebot
ENV_FILE=/etc/cppgram-livebot.env
DATA=/var/lib/cppgram-livebot
LOGS=/var/log/cppgram-livebot
USER_NAME=cppgram-livebot

for tool in systemctl useradd install; do
    command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }
done

if ! id -u "$USER_NAME" >/dev/null 2>&1; then
    useradd --system --home-dir "$DATA" --shell /usr/sbin/nologin "$USER_NAME"
fi

install -d -o root -g root -m 0755 "$APP" "$APP/bin"
install -d -o "$USER_NAME" -g "$USER_NAME" -m 0700 "$DATA" "$DATA/tmp"
install -d -o "$USER_NAME" -g "$USER_NAME" -m 0750 "$LOGS"

install -o root -g root -m 0755 "$BIN_SRC" "$APP/bin/cppgram_live_test_bot"
install -o root -g root -m 0644 "$HERE/../README.md" "$APP/README.md"
install -o root -g root -m 0755 "$HERE/livebotctl.sh" "$APP/livebotctl.sh"
install -o root -g root -m 0644 "$HERE/cppgram-livebot.service" /etc/systemd/system/cppgram-livebot.service
install -o root -g root -m 0644 "$HERE/logrotate.conf" /etc/logrotate.d/cppgram-livebot

if [[ ! -e "$ENV_FILE" ]]; then
    install -o root -g root -m 0600 "$HERE/env.example" "$ENV_FILE"
    echo "created $ENV_FILE from the example. Fill in the CHANGE_ME values before starting."
else
    chown root:root "$ENV_FILE"
    chmod 0600 "$ENV_FILE"
    echo "kept existing $ENV_FILE (permissions set to 0600)"
fi

systemctl daemon-reload
systemctl enable cppgram-livebot.service >/dev/null
echo "installed. enabled at boot. not started yet."
echo "next: edit $ENV_FILE, then run: $APP/livebotctl.sh check-env && $APP/livebotctl.sh start"
