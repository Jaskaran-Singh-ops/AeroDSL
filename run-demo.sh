#!/bin/sh
# Starts the AeroDSL demonstration page and opens it in your browser.
# Stop it with Ctrl-C.
cd "$(dirname "$0")" || exit 1
PORT=${1:-8080}

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)              BIN=bin/macos-arm64/aerodsl ;;
  Darwin-*)                  BIN=bin/macos-x64/aerodsl ;;
  Linux-aarch64|Linux-arm64) BIN=bin/linux-arm64/aerodsl ;;
  Linux-*)                   BIN=bin/linux-x64/aerodsl ;;
  *)                         BIN= ;;
esac
# A local build (from `make`) wins over the prebuilt program.
[ -x ./aerodsl ] && BIN=./aerodsl
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
  echo "No program for this system. Build one with: make" >&2
  exit 1
fi

URL="http://127.0.0.1:$PORT"
( sleep 1
  if [ "$(uname -s)" = Darwin ]; then open "$URL"
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$URL" >/dev/null 2>&1
  else echo "Open $URL in your browser."; fi ) &
exec "$BIN" --serve "$PORT"
