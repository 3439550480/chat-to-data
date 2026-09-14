#!/bin/bash
# H15 收尾：重编 AIService → 重启 → 重跑 E2E
cd /home/dev/chat2data/svc_aiService/build
OUT=/home/dev/chat2data/test/svc_aiService/final_run.txt
{
echo "=== 重编 ==="
make -j4 2>&1 | grep -E 'error|Built target' | tail -3
} > "$OUT" 2>&1

pkill -f 'AIServic[e]' 2>/dev/null
sleep 1
cd /home/dev/chat2data/svc_aiService/build
(./AIService > /tmp/ai.log 2>&1 &)
sleep 8
{
timeout 1 bash -c 'echo > /dev/tcp/127.0.0.1/9006' 2>/dev/null && echo "9006 在听" || echo "9006 未监听"
echo "=== E2E ==="
cd /home/dev/chat2data/test/svc_aiService
bash e2e_ai.sh
echo "e2e_exit=$?"
} >> "$OUT" 2>&1
