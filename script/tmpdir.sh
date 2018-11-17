#!/bin/sh

set -euo pipefail

TMPDIR=$(mktemp -dq)

trap "exit 1" HUP INT PIPE QUIT TERM
trap "rm -rf $TMPDIR" EXIT

cd "$TMPDIR"
eval "$@"
exit $?
