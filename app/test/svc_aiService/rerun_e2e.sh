#!/bin/bash
# 检查 .env 与重启 AIService 并重跑 E2E（H15 收官脚本）
cd /home/dev/chat2data/test/svc_aiService
OUT=restart_result.txt
{
echo "=== .env 检查（只看长度）==="
awk -F= '/^GLM_/ {printf "%s = len:%d\n", $1, length($2)}' /home/dev/chat2data/.env
} > "$OUT" 2>&1

# 重启 AIService（先杀后启，分两步避免 pkill 自匹配）
pkill -f 'AIServic[e]' 2>/dev/null
sleep 1
cd /home/dev/chat2data/svc_aiService/build
(./AIService > /tmp/ai.log 2>&1 &)
sleep 8
timeout 1 bash -c 'echo > /dev/tcp/127.0.0.1/9006' 2>/dev/null && echo "9006 在听" >> "$OUT" || echo "9006 未监听" >> "$OUT"
grep -E 'GLMProvider' /tmp/ai.log | head -2 >> "$OUT" 2>&1

# 重跑 E2E
echo "=== e2e_ai.sh ===" >> "$OUT"
cd /home/dev/chat2data/test/svc_aiService
bash e2e_ai.sh >> "$OUT" 2>&1
echo "e2e_exit=$?" >> "$OUT"
