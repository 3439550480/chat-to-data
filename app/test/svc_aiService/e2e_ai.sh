#!/bin/bash
# H15b：AI 服务跨服务端到端（HTTP + JSON，与真实网关同路径）
# 覆盖 5 个 RPC：CreateSession / GetSessions / SendMessage(SSE, 真 GLM) /
#               GetSessionHistory / DeleteSession
# 用法：容器内 bash e2e_ai.sh（结果输出到 stdout，failCount=0 即通过）
set -u
BASE="http://127.0.0.1:9006/chat2Data.AiService.AIService"
USER="e2e_user"
SESSION="e2e_session"
MODEL="glm-5.3-flash"
FAIL=0
ok()  { echo "[OK]   $1"; }
bad() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }

# 1. CreateSession（plain 场景）
resp=$(curl -s --max-time 30 -X POST "$BASE/CreateSession" -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"e2e_1\",\"user_id\":\"$USER\",\"model\":\"$MODEL\",\"session_type\":\"plain\"}")
csid=$(echo "$resp" | sed -E 's/.*"chat_session_id":"([^"]*)".*/\1/')
if [ -n "$csid" ] && [ "$csid" != "$resp" ]; then
  ok "CreateSession (chat_session_id=$csid)"
else
  bad "CreateSession: $resp"
fi

# 2. GetSessions：包含新会话
resp=$(curl -s --max-time 30 -X POST "$BASE/GetSessions" -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"e2e_2\",\"user_id\":\"$USER\"}")
if echo "$resp" | grep -q "$csid"; then
  ok "GetSessions contains the new session"
else
  bad "GetSessions missing session: $resp"
fi

# 3. SendMessage（plain，真 GLM，SSE 流式）
resp=$(curl -s -N --max-time 120 -X POST "$BASE/SendMessage" -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"e2e_3\",\"session_id\":\"$SESSION\",\"user_id\":\"$USER\",\"chat_session_id\":\"$csid\",\"chat_type\":\"plain\",\"message\":\"请只回复四个字：连接成功\"}")
# SSE 块以 \n\n 结束；curl 输出末尾可能无换行 → 先补换行再按行精确匹配
# （grep -q 会被内容中偶现的 [DONE] 字样干扰，必须整行匹配 '^data: \[DONE\]$'）
if echo "$resp" | sed -e 's/$//' | grep -qx 'data: \[DONE\]'; then
  ok "SendMessage SSE stream finished (data: [DONE] received)"
else
  bad "SendMessage no DONE marker: $resp"
fi
content=$(echo "$resp" | grep '^data: ' | grep -v '\[DONE\]' | head -1)
if [ -n "$content" ]; then
  ok "SendMessage non-empty model content"
else
  bad "SendMessage empty model content"
fi

# 4. GetSessionHistory：消息 ≥1 条 user + ≥1 条 assistant
resp=$(curl -s --max-time 30 -X POST "$BASE/GetSessionHistory" -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"e2e_4\",\"user_id\":\"$USER\",\"chat_session_id\":\"$csid\"}")
assistantCount=$(echo "$resp" | grep -o '"role":"assistant"' | wc -l)
userCount=$(echo "$resp" | grep -o '"role":"user"' | wc -l)
if [ "$assistantCount" -ge 1 ] && [ "$userCount" -ge 1 ]; then
  ok "GetSessionHistory has user+assistant messages ($userCount/$assistantCount)"
else
  bad "GetSessionHistory messages missing: $resp"
fi

# 5. DeleteSession → 成功且列表不再包含
# 注意：brpc pb2json 省略默认值字段（error_code=0 不出现在 JSON 中），
#       成功判定 = 不存在非零 error_code
resp=$(curl -s --max-time 30 -X POST "$BASE/DeleteSession" -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"e2e_5\",\"user_id\":\"$USER\",\"chat_session_id\":\"$csid\"}")
if echo "$resp" | grep -q '"error_code":[1-9]'; then
  bad "DeleteSession: $resp"
else
  ok "DeleteSession success"
fi
resp=$(curl -s --max-time 30 -X POST "$BASE/GetSessions" -H 'Content-Type: application/json' \
  -d "{\"request_id\":\"e2e_6\",\"user_id\":\"$USER\"}")
if echo "$resp" | grep -q "$csid"; then
  bad "session still listed after delete"
else
  ok "session removed from list after delete"
fi

echo "summary: failCount=$FAIL"
[ $FAIL -eq 0 ] && exit 0 || exit 1
