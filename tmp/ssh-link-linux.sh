#!/usr/bin/env bash
set -euo pipefail
cd /mnt/c/Users/Gargadon/Source/banchoxterm
cmake -S . -B build/linux-debug -DFETCHCONTENT_FULLY_DISCONNECTED=ON > build/ssh-link-linux-configure.log 2>&1
cmake --build build/linux-debug --target banchoxterm banchoxterm_tests --parallel 4 > build/ssh-link-linux-build.log 2>&1
QT_QPA_PLATFORM=offscreen build/linux-debug/banchoxterm_tests testSpecialCharacterBytes testSshBreakPacket testVtTerminalSpecialKeys testTerminalBellNotification -o build/ssh-link-linux-tests.txt,txt
find src tests -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format-18 --dry-run --Werror
