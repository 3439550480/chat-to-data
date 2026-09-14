#!/bin/bash
# 直接用 curl 验证 GLM key 有效性（绕开我们的全部代码）
cd /home/dev/chat2data
OUT=test/svc_aiService/keytest.txt
KEY=$(grep '^GLM_API_KEY=' .env | cut -d= -f2)
MODEL=$(grep '^GLM_MODEL_NAME=' .env | cut -d= -f2)
{
echo "key len: ${#KEY}, model: $MODEL"
echo "--- 非流式直测（脱离我们的代码）---"
code=$(curl -s -o body.json -w "%{http_code}" --max-time 60 \
  -X POST "https://open.bigmodel.cn/api/paas/v4/chat/completions" \
  -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
  -d "{\"model\":\"$MODEL\",\"messages\":[{\"role\":\"user\",\"content\":\"只回复：OK\"}],\"max_tokens\":10}")
echo "HTTP status: $code"
head -c 500 body.json
echo ""
} > "$OUT" 2>&1
