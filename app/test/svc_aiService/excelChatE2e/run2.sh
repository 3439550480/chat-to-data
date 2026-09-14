#!/bin/bash
# 修复后重建 + 重启 AIService + 重启 FDFS storage + 重跑 excel E2E
OUT=/home/dev/chat2data/test/svc_aiService/excel_run2.txt
{
echo "=== 1. 重建 AIService ==="
cd /home/dev/chat2data/svc_aiService/build
make -j4 2>&1 | grep -E 'error|Built target' | tail -3
} > "$OUT" 2>&1

echo "=== 2. 重启 AIService ===" >> "$OUT"
pkill -f 'AIServic[e]' 2>/dev/null
sleep 1
cd /home/dev/chat2data/svc_aiService/build
(./AIService > /tmp/ai.log 2>&1 &)
sleep 8
timeout 1 bash -c 'echo > /dev/tcp/127.0.0.1/9006' 2>/dev/null && echo "9006 在听" >> "$OUT" || echo "9006 未监听" >> "$OUT"

{
echo "=== 3. 四服务状态 ==="
for p in 9003 9004 9005 9006; do
  timeout 1 bash -c "echo > /dev/tcp/127.0.0.1/$p" 2>/dev/null && echo "$p 在听" || echo "$p 未运行"
done
echo "=== 4. 等待 etcd 服务发现稳定 ==="
sleep 5
grep -cE 'Service online' /tmp/ai.log
} >> "$OUT" 2>&1

echo "=== 5. 重跑 excel E2E ===" >> "$OUT"
cd /home/dev/chat2data/test/svc_aiService/excelChatE2e/build
./excelChatE2e >> "$OUT" 2>&1
echo "run_exit=$?" >> "$OUT"
