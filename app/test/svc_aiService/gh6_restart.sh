#!/bin/bash
# G-H6 收尾：重启网关（加载注释修正后的二进制）+ 快速回归 /health
OUT=/home/dev/chat2data/tmp/gh6_restart.txt
{
pkill -f GatewayService
sleep 1
cd /home/dev/chat2data/svc_gatewayService/build && (./GatewayService > /tmp/gw.log 2>&1 &)
sleep 3
timeout 1 bash -c "echo > /dev/tcp/127.0.0.1/9000" 2>/dev/null && echo "9000 在听" || echo "9000 未运行"
curl -s --max-time 5 http://127.0.0.1:9000/health
} > "$OUT" 2>&1
