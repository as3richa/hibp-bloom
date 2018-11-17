#!/bin/sh

set -euo pipefail

if [ "$#" != "1" ]; then
  exit 1
fi

echo $(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
