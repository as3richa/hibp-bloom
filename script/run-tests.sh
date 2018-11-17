#!/bin/sh

set -euo pipefail

if [ "$#" = "0" ]; then
  echo 'Run the test suite with `make tests`'
  exit 1
fi

SUITE_FAILED="0"

for TEST_COMMAND in "$@"; do
  PRETTY_NAME=$(basename "$TEST_COMMAND")

  ABSOLUTE_COMMAND=$(script/abspath.sh "$TEST_COMMAND")
  RELATIVE_COMMAND="$TEST_COMMAND"

  if [ ! -z ${VALGRIND+1} ]; then
    PRETTY_NAME="$PRETTY_NAME (valgrind)"

    ABSOLUTE_COMMAND="$(script/abspath.sh script/valgrind.sh) \"$ABSOLUTE_COMMAND\" $(script/abspath.sh "$SUPPRESSIONS")"
    RELATIVE_COMMAND="script/valgrind.sh \"$RELATIVE_COMMAND\" \"$SUPPRESSIONS\""
  fi

  if script/tmpdir.sh "$ABSOLUTE_COMMAND" 1>/dev/null 2>&1; then
    echo "[PASSED] $PRETTY_NAME"
  else
    echo "[FAILED] $PRETTY_NAME; rerun manully with: $RELATIVE_COMMAND"
    SUITE_FAILED="1"
  fi
done

exit $SUITE_FAILED
