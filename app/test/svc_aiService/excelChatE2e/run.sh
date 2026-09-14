#!/bin/bash
# 构建并运行 excel 场景全链路 E2E
cd /home/dev/chat2data/test/svc_aiService/excelChatE2e
OUT=/home/dev/chat2data/test/svc_aiService/excel_e2e_result.txt
{
mkdir -p build && cd build && cmake .. > /dev/null 2>&1 && make -j4 2>&1 | grep -E 'error|Built target'
if [ -f excelChatE2e ]; then
  echo "=== 运行 ==="
  ./excelChatE2e
  echo "run_exit=$?"
fi
} > "$OUT" 2>&1
