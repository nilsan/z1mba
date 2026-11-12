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
