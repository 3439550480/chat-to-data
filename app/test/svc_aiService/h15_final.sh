#!/bin/bash
# H15 收官整合脚本：FDFS 检查 → 服务拉起 → 重建/重启 AIService → excel E2E → plain E2E 回归
OUT=/home/dev/chat2data/test/svc_aiService/h15_final.txt
{
echo "=== 0. FDFS storage 注册状态 ==="
fdfs_monitor /etc/fdfs/client.conf 2>/dev/null | grep -E 'ip_addr|status' | head -8
if [ ! -s /tmp/.fdfs_seen ]; then
  fdfs_monitor /etc/fdfs/client.conf 2>/dev/null | head -30
fi
echo "=== 1. 服务健康检查/拉起 ==="
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
echo "=== 2. 重建 AIService（AI1-附加/AI12 修复）==="
cd /home/dev/chat2data/svc_aiService/build
make -j4 2>&1 | grep -E 'error|Built target' | tail -3
echo "=== 3. 重启 AIService ==="
pkill -f 'AIServic[e]' 2>/dev/null
sleep 1
(./AIService > /tmp/ai.log 2>&1 &)
sleep 8
timeout 1 bash -c 'echo > /dev/tcp/127.0.0.1/9006' 2>/dev/null && echo "9006 在听" || echo "9006 未监听"
echo "=== 4. excel E2E ==="
cd /home/dev/chat2data/test/svc_aiService/excelChatE2e/build
./excelChatE2e
echo "excel_exit=$?"
echo "=== 5. plain E2E 回归 ==="
cd /home/dev/chat2data/test/svc_aiService
bash e2e_ai.sh
echo "e2e_exit=$?"
} > "$OUT" 2>&1
