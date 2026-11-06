#!/bin/bash
# 云端运行原型：run.sh <opengl|vulkan> <其余参数...>
B=$1; shift
export DISPLAY=:99 QT_QPA_PLATFORM=xcb
export LD_LIBRARY_PATH=/home/claude/w/install-$B/lib:/opt/qtenv/lib
export XDG_CACHE_HOME=/home/claude/w/xdgcache
exec ${TP_EXE:-/home/claude/w/build-app/TravelPlanner} --backend=${BK:-$B} --data /home/claude/w/testdata/data.js "$@"
