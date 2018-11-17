#!/bin/sh

set -euo pipefail

echo "Generating valgrind suppressions list..." >&2

CC=$1
SOURCE=$2

"$CC" -O0 -lcrypto -o supp "$SOURCE"
valgrind --tool=memcheck --gen-suppressions=all --leak-check=full ./supp 2>valgrind.out

STATE="0"
IFS=

while read LINE; do
  if [ "$LINE" = "{" ]; then
    if [ "$STATE" = "1" ]; then
      exit 1
    fi
    STATE="1"
  elif [ "$LINE" = "}" ]; then
    echo "}"
    STATE="0"
  fi

  if [ "$STATE" = "1" ]; then
    echo "$LINE"
  fi
done <valgrind.out

echo "Done!" >&2
