#!/bin/bash
# P4 浏览器问题冒烟：用户信息 / 文件列表 / 创建会话（部署态网关，合法会话）
# 用户报告：1.文件列表失败 2.用户信息失败 3.上传失败 4.创建会话失败
# 回归（user+ai 链路）全绿但浏览器 4 接口失败 → 逐接口取证
set -u
BASE="http://dev-gateway:9000"
TAG="smoke$(date +%s)"
EMAIL="smoke_${TAG}@test.com"
PASS="Smoke123456"
rid()  { echo "req_$(date +%s%N)$RANDOM"; }

resp=$(curl -s --max-time 10 -X POST "$BASE/api/user/register" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"nickname\":\"smoke_${TAG}\",\"password\":\"$PASS\",\"email\":\"$EMAIL\"}")
echo "[1.register] $resp"

resp=$(curl -s --max-time 10 -X POST "$BASE/api/user/passwd/login" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"username\":\"$EMAIL\",\"password\":\"$PASS\"}")
SID=$(echo "$resp" | sed -E 's/.*"sessionId":"([^"]*)".*/\1/')
echo "[2.login] SID=${SID:0:20}... (resp head: $(echo "$resp" | head -c 120))"

if [ -z "$SID" ] || [ "$SID" = "$resp" ]; then
  echo "[FATAL] no sessionId, abort"; exit 1
fi

echo "[3.user/info]"
curl -s --max-time 10 -X POST "$BASE/api/user/info" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\"}"
echo ""

echo "[4.file/list POST]"
curl -s --max-time 10 -X POST "$BASE/api/file/list" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\"}"
echo ""

echo "[5.file/list GET]"
curl -s --max-time 10 "$BASE/api/file/list?requestId=$(rid)&sessionId=$SID"
echo ""

echo "[6.ai/session/create]"
curl -s --max-time 15 -X POST "$BASE/api/ai/session/create" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"modelName\":\"glm-5.3-flash\",\"sessionType\":\"plain\"}"
echo ""
