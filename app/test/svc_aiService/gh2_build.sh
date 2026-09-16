#!/bin/bash
# G-H2 编译验证（网关）
cd /home/dev/chat2data/svc_gatewayService/build
OUT=/home/dev/chat2data/tmp/gh2_build.txt
{
cmake .. > /dev/null 2>&1
make -j4 2>&1 | grep -E 'error|Built target' | head -12
echo "build_exit=$?"
} > "$OUT" 2>&1
