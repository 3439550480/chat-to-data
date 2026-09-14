#!/bin/bash
# 聚焦诊断：AIService 状态 + GLM 调用详情
cd /home/dev/chat2data/test/svc_aiService
OUT=diag2.txt
{
echo "=== 进程/端口 ==="
pgrep -f 'AIServic[e]' >/dev/null && echo "AI进程存活" || echo "AI进程不存在"
timeout 1 bash -c 'echo > /dev/tcp/127.0.0.1/9006' 2>/dev/null && echo "9006 在听" || echo "9006 未监听"
echo "=== ai.log GLM/错误行 ==="
grep -E 'GLMProvider|sendMessageStream|401|network error' /tmp/ai.log | tail -8
echo "=== 单条 SendMessage 原始响应 ==="
csid=$(curl -s --max-time 30 -X POST 'http://127.0.0.1:9006/chat2Data.AiService.AIService/CreateSession' \
  -H 'Content-Type: application/json' \
  -d '{"request_id":"dbg10","user_id":"e2e_user","model":"glm-5.3-flash","session_type":"plain"}' \
  | sed -E 's/.*"chat_session_id":"([^"]*)".*/\1/')
echo "csid=$csid"
curl -s -N --max-time 120 -X POST 'http://127.0.0.1:9006/chat2Data.AiService.AIService/SendMessage' \
  -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"dbg11\",\"session_id\":\"e2e_s\",\"user_id\":\"e2e_user\",\"chat_session_id\":\"$csid\",\"chat_type\":\"plain\",\"message\":\"hi\"}"
echo ""
echo "=== ai.log GLM/错误行（第二次）==="
grep -E 'GLMProvider|sendMessageStream|401|network error' /tmp/ai.log | tail -4
} > "$OUT" 2>&1
