#!/bin/bash
# G-H5 编译验证
cd /home/dev/chat2data/svc_gatewayService/build
OUT=/home/dev/chat2data/tmp/gh5_build.txt
{
make -j4 2>&1 | grep -E 'error|Error|Built target' | head -15
echo "build_exit=$?"
} > "$OUT" 2>&1
