#!/bin/bash
# H15b 调试：单条 SendMessage 链路追踪
cd /home/dev/chat2data/test/svc_aiService
OUT=debug_result.txt

# 1. 创建会话
csid=$(curl -s --max-time 30 -X POST 'http://127.0.0.1:9006/chat2Data.AiService.AIService/CreateSession' \
  -H 'Content-Type: application/json' \
  -d '{"request_id":"dbg1","user_id":"e2e_user","model":"glm-5.3-flash","session_type":"plain"}' \
  | sed -E 's/.*"chat_session_id":"([^"]*)".*/\1/')
echo "=== session: $csid" > "$OUT"

# 2. SendMessage（plain）
curl -s -N --max-time 120 -X POST 'http://127.0.0.1:9006/chat2Data.AiService.AIService/SendMessage' \
  -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"dbg2\",\"session_id\":\"e2e_s\",\"user_id\":\"e2e_user\",\"chat_session_id\":\"$csid\",\"chat_type\":\"plain\",\"message\":\"hi\"}" \
  >> "$OUT" 2>&1
echo "" >> "$OUT"

# 3. AIService 日志尾部（模型调用链路）
echo "=== ai.log tail ===" >> "$OUT"
grep -E 'GLMProvider|sendMessageStream|LLMManager|sendMessage' /tmp/ai.log | tail -15 >> "$OUT" 2>&1
