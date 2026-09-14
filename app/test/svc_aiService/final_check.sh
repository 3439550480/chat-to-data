#!/bin/bash
# H15 收官脚本：新 Key 直测 → 重启 AIService → 重跑 E2E
cd /home/dev/chat2data
OUT=test/svc_aiService/final_check.txt
{
echo "=== 1. curl 直测 GLM（新 key）==="
KEY=$(grep '^GLM_API_KEY=' .env | cut -d= -f2)
MODEL=$(grep '^GLM_MODEL_NAME=' .env | cut -d= -f2)
echo "key len: ${#KEY}, model: $MODEL"
code=$(curl -s -o /tmp/body.json -w "%{http_code}" --max-time 60 \
  -X POST "https://open.bigmodel.cn/api/paas/v4/chat/completions" \
  -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
  -d "{\"model\":\"$MODEL\",\"messages\":[{\"role\":\"user\",\"content\":\"只回复：OK\"}],\"max_tokens\":10}")
echo "HTTP status: $code"
head -c 300 /tmp/body.json
echo ""

if [ "$code" = "200" ]; then
  echo "=== 2. 重启 AIService ==="
  pkill -f 'AIServic[e]' 2>/dev/null
  sleep 1
  cd /home/dev/chat2data/svc_aiService/build
  (./AIService > /tmp/ai.log 2>&1 &)
  sleep 8
  timeout 1 bash -c 'echo > /dev/tcp/127.0.0.1/9006' 2>/dev/null && echo "9006 在听" || echo "9006 未监听"

  echo "=== 3. 重跑 E2E ==="
  cd /home/dev/chat2data/test/svc_aiService
  bash e2e_ai.sh
  echo "e2e_exit=$?"
else
  echo "key 仍无效，跳过重启与 E2E"
fi
} > "$OUT" 2>&1
