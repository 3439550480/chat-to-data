#!/bin/bash
# G-H3 编译验证
cd /home/dev/chat2data/svc_gatewayService/build
OUT=/home/dev/chat2data/tmp/gh3_build.txt
{
make -j4 2>&1 | grep -E 'error|Built target' | head -12
echo "build_exit=$?"
} > "$OUT" 2>&1
