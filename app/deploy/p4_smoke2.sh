#!/bin/bash
# P4 全序列冒烟 v2：按前端真实姿势逐接口验证部署态
# 前端姿势依据：api.js/file.js/auth.js（GET info + 两步上传 + octet-stream）
set -u
BASE="http://dev-gateway:9000"
TAG="s2$(date +%s)"
EMAIL="s2_${TAG}@test.com"
PASS="Smoke123456"
rid()  { echo "req_$(date +%s%N)$RANDOM"; }

curl -s --max-time 10 -X POST "$BASE/api/user/register" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"nickname\":\"s2_${TAG}\",\"password\":\"$PASS\",\"email\":\"$EMAIL\"}" > /dev/null
resp=$(curl -s --max-time 10 -X POST "$BASE/api/user/passwd/login" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"username\":\"$EMAIL\",\"password\":\"$PASS\"}")
SID=$(echo "$resp" | sed -E 's/.*"sessionId":"([^"]*)".*/\1/')
echo "[login] SID=${SID:0:20}..."

echo "[1.user/info GET]"
curl -s --max-time 10 "$BASE/api/user/info?requestId=$(rid)&sessionId=$SID"
echo ""

echo "[2.file/upload/info]"
resp=$(curl -s --max-time 10 -X POST "$BASE/api/file/upload/info" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"fileInfo\":{\"filename\":\"test.xlsx\",\"fileSize\":1443,\"fileExt\":\"xlsx\"}}")
echo "$resp"
FID=$(echo "$resp" | sed -E 's/.*"fileId":"([^"]*)".*/\1/')
echo "[2b.fileId] ${FID:0:28}..."

echo "[3.file/upload octet-stream]"
curl -s --max-time 30 -X POST "$BASE/api/file/upload?requestId=$(rid)&sessionId=$SID&fileId=$FID" \
  -H 'Content-Type: application/octet-stream' --data-binary @/tmp/test.xlsx
echo ""

echo "[4.file/list]"
curl -s --max-time 10 -X POST "$BASE/api/file/list" -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\"}"
echo ""

echo "[5.file/preview]"
if [ -n "$FID" ] && [ "$FID" != "$resp" ]; then
  curl -s --max-time 15 -X POST "$BASE/api/file/preview" -H 'Content-Type: application/json' \
    -d "{\"requestId\":\"$(rid)\",\"sessionId\":\"$SID\",\"fileId\":\"$FID\",\"sheetName\":\"\",\"page\":1,\"pageSize\":20}"
  echo ""
else
  echo "(skip, no fileId)"
fi
