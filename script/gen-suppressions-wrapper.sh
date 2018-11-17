#!/bin/bash

set -euo pipefail

CC="$1"
SOURCE=$(script/abspath.sh "$2")
SCRIPT=$(script/abspath.sh script/gen-suppressions.sh)

exec script/tmpdir.sh "$SCRIPT" "$CC" "$SOURCE" 
