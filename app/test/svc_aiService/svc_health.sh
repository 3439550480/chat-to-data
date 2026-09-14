#!/bin/bash
# 服务健康检查 + 按依赖顺序拉起缺失的服务
OUT=/home/dev/chat2data/test/svc_aiService/svc_status.txt
{
for p in 9003 9004 9005 9006; do
  if timeout 1 bash -c "echo > /dev/tcp/127.0.0.1/$p" 2>/dev/null; then
    echo "$p 在听"
  else
    echo "$p 未运行 → 拉起"
    case $p in
      9003) cd /home/dev/chat2data/svc_excelParseService/build && (./ExcelParserService > /tmp/ep.log 2>&1 &) ;;
      9004) cd /home/dev/chat2data/svc_fileService/build && (./FileService > /tmp/fs.log 2>&1 &) ;;
      9005) cd /home/dev/chat2data/svc_dbService/build && (./DatabaseService > /tmp/db.log 2>&1 &) ;;
      9006) cd /home/dev/chat2data/svc_aiService/build && (./AIService > /tmp/ai.log 2>&1 &) ;;
    esac
  fi
done
sleep 10
echo "=== 拉起后状态 ==="
for p in 9003 9004 9005 9006; do
  timeout 1 bash -c "echo > /dev/tcp/127.0.0.1/$p" 2>/dev/null && echo "$p 在听" || echo "$p 未运行"
done
} > "$OUT" 2>&1
