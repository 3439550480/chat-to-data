#!/bin/bash
# G-H6：网关全链路 E2E（注册 → 密码登录 → AI 6 接口 → SSE 透传验真 GLM）
# 与 H15b e2e_ai.sh 的区别：本脚本走网关 9000 的 HTTP 接口（camelCase + sessionId 鉴权），
# 覆盖 G-H2（用户）/G-H5（AI）落盘代码，重点验证 handleAiChat 的 SSE 透传。
# 用法：容器内 bash gh6_gateway_test.sh > 结果文件
set -u
BASE="http://127.0.0.1:9000"
MODEL="glm-5.3-flash"
TAG="gh6$(date +%s)"                      # 随机后缀保证幂等（重复跑不撞已注册用户）
NICK="gh6tester_${TAG}"
EMAIL="gh6_${TAG}@test.com"
PASS="Gh6Pass123"
FAIL=0
ok()   { echo "[OK]   $1"; }
bad()  { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
info() { echo "[INFO] $1"; }
rid()  { echo "req_$(date +%s%N)$RANDOM"; }

# 0. /health 探活
health=$(curl -s --max-time 5 "$BASE/health")
if echo "$health" | grep -q '"healthy"'; then
  ok "GET /health healthy"
else
  bad "GET /health: $health"
fi

# 1. 注册（失败不阻断——可能是重复跑撞库）
resp=$(curl -s --max-time 10 -X POST "$BASE/api/user/register" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"nickname\":\"$NICK\",\"password\":\"$PASS\",\"email\":\"$EMAIL\"}")
if echo "$resp" | grep -q '"errorCode":0'; then
  ok "register: $EMAIL"
else
  info "register（非阻断）: $resp"
fi

# 2. 密码登录拿 sessionId（G-H2 handlePasswordLogin）
resp=$(curl -s --max-time 10 -X POST "$BASE/api/user/passwd/login" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"username\":\"$EMAIL\",\"password\":\"$PASS\"}")
SID=$(echo "$resp" | sed -E 's/.*"sessionId":"([^"]*)".*/\1/')
if [ -n "$SID" ] && [ "$SID" != "$resp" ]; then
  ok "passwd/login (sessionId=${SID:0:16}...)"
else
  bad "passwd/login: $resp"
  echo "summary: failCount=$FAIL"; exit 1
fi

# 3. GetModels（G-H5 5.1）
resp=$(curl -s --max-time 10 -X POST "$BASE/api/ai/models" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\"}")
if echo "$resp" | grep -q 'modelList'; then
  ok "GetModels has modelList: $(echo "$resp" | head -c 200)"
else
  bad "GetModels: $resp"
fi

# 4. CreateSession（G-H5 5.2，plain 场景）
resp=$(curl -s --max-time 15 -X POST "$BASE/api/ai/session/create" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"modelName\":\"$MODEL\",\"sessionType\":\"plain\"}")
CSID=$(echo "$resp" | sed -E 's/.*"chatSessionId":"([^"]*)".*/\1/')
if [ -n "$CSID" ] && [ "$CSID" != "$resp" ]; then
  ok "CreateSession (chatSessionId=${CSID:0:24}...)"
else
  bad "CreateSession: $resp"
  echo "summary: failCount=$FAIL"; exit 1
fi

# 5. chatSessionLists 包含新会话（G-H5 5.3）
resp=$(curl -s --max-time 10 -X POST "$BASE/api/ai/chatSessionLists" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\"}")
if echo "$resp" | grep -q "$CSID"; then
  ok "chatSessionLists contains new session"
else
  bad "chatSessionLists missing session: $(echo "$resp" | head -c 300)"
fi

# 6. sendStreamMessage（G-H5 5.6 压轴：SSE 透传 + 真 GLM）
resp=$(curl -s -N --max-time 150 -X POST "$BASE/api/ai/sendStreamMessage" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"chatSessionId\":\"$CSID\",\"chatType\":\"plain\",\"message\":\"请只回复四个字：连接成功\"}")
# 6a. [DONE] 计数：AI 侧 1 次 + 网关兜底 1 次 = 2（GW2 保留双标记为预期行为）
doneCount=$(echo "$resp" | grep -cx 'data: \[DONE\]')
if [ "$doneCount" -ge 1 ]; then
  ok "sendStreamMessage stream finished (data: [DONE] x$doneCount — GW2 双标记预期值 2)"
else
  bad "sendStreamMessage no [DONE]: $(echo "$resp" | head -c 300)"
fi
# 6b. 流式非空内容块（透传到了 GLM 增量）
blk=$(echo "$resp" | grep '^data: ' | grep -v '\[DONE\]' | head -1)
if [ -n "$blk" ]; then
  ok "sendStreamMessage first chunk: $(echo "$blk" | head -c 140)..."
else
  bad "sendStreamMessage empty stream (no data chunks)"
fi

# 7. GetHistory：user+assistant 消息都在（G-H5 5.4）
resp=$(curl -s --max-time 10 -X POST "$BASE/api/ai/history" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"chatSessionId\":\"$CSID\"}")
uc=$(echo "$resp" | grep -o '"role":"user"' | wc -l)
ac=$(echo "$resp" | grep -o '"role":"assistant"' | wc -l)
if [ "$ac" -ge 1 ] && [ "$uc" -ge 1 ]; then
  ok "GetHistory user=$uc assistant=$ac"
else
  bad "GetHistory (user=$uc assistant=$ac): $(echo "$resp" | head -c 300)"
fi

# 8. DeleteSession → 列表清理（G-H5 5.5）
resp=$(curl -s --max-time 10 -X POST "$BASE/api/ai/delete" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"chatSessionId\":\"$CSID\"}")
if echo "$resp" | grep -q '"errorCode":[1-9]'; then
  bad "DeleteSession: $resp"
else
  ok "DeleteSession success"
fi
resp=$(curl -s --max-time 10 -X POST "$BASE/api/ai/chatSessionLists" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\"}")
if echo "$resp" | grep -q "$CSID"; then
  bad "session still listed after delete"
else
  ok "session removed from list after delete"
fi

echo "summary: failCount=$FAIL"
[ $FAIL -eq 0 ] && exit 0 || exit 1
