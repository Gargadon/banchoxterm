#!/bin/bash
set -eu
cd /mnt/c/Users/Gargadon/Source/banchoxterm
export QT_QPA_PLATFORM=offscreen
gdb -batch -ex run -ex 'thread apply all bt' --args build/linux-debug/banchoxterm_tests testSshAndSftpAgainstLocalServer > build/linux-gdb.log 2>&1
