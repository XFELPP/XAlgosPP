#!/bin/sh
set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -f "$SRC_DIR/configure" ]; then
  (cd "$SRC_DIR" && ./autogen.sh)
fi

exec "$SRC_DIR/configure" "$@"
