#!/bin/sh

set -euo pipefail

if [ "$#" != 2 ]; then
  exit 1
fi

COMMAND="$1"
SUPPRESSIONS_FILE="$2"

exec valgrind \
  --tool=memcheck \
  --leak-check=yes \
  --track-origins=yes \
  --error-exitcode=1 \
  "--suppressions=$SUPPRESSIONS_FILE" \
  --quiet \
  -- "$COMMAND"
