#!/bin/sh
# Build and run the native tests. The headers under test have no ESPHome
# dependency, so plain g++ builds them, with AddressSanitizer and UBSan.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp)
trap 'rm -f "$out"' EXIT
build() { g++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined "$@" -o "$out" -pthread; }
build "$here/test_z1_proto.cpp" && "$out"
build "$here/test_z1_tracker.cpp" && "$out" "$here/fixtures/studio-session.txt"
build "$here/test_z1_client.cpp" && "$out"

# The screen, at the size of every preset in screens/. Z1_SVG=dir also writes
# a preview of each preset and state there.
presets=$(python3 "$here/../tools/screen_presets.py")
# shellcheck disable=SC2086  # one word per field
build "$here/test_z1_screen.cpp" && "$out" $presets ${Z1_SVG:+--svg "$Z1_SVG"}
