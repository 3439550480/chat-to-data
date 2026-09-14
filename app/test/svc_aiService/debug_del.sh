#!/bin/bash
# 调试：DeleteSession 为何失败（与 GLM 无关，单独验证）
cd /home/dev/chat2data/test/svc_aiService
OUT=debug_del.txt

csid=$(curl -s --max-time 30 -X POST 'http://127.0.0.1:9006/chat2Data.AiService.AIService/CreateSession' \
  -H 'Content-Type: application/json' \
  -d '{"request_id":"d1","user_id":"e2e_user","model":"glm-5.3-flash","session_type":"plain"}' \
  | sed -E 's/.*"chat_session_id":"([^"]*)".*/\1/')
echo "=== session: $csid" > "$OUT"

resp=$(curl -s --max-time 30 -X POST 'http://127.0.0.1:9006/chat2Data.AiService.AIService/DeleteSession' \
  -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"d2\",\"user_id\":\"e2e_user\",\"chat_session_id\":\"$csid\"}")
echo "=== DeleteSession resp ===" >> "$OUT"
echo "$resp" >> "$OUT"
