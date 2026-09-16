#!/bin/bash
# G-H6 环境拉起：补 user(9001)/notify(9002)，核对 7 端口
OUT=/home/dev/chat2data/tmp/gh6_up.txt
{
cd /home/dev/chat2data/svc_userService/build && (./UserService > /tmp/us.log 2>&1 &)
cd /home/dev/chat2data/svc_notifyService/build && (./NotifyService > /tmp/ns.log 2>&1 &)
sleep 10
for p in 9001 9002 9003 9004 9005 9006 9000; do
  timeout 1 bash -c "echo > /dev/tcp/127.0.0.1/$p" 2>/dev/null && echo "$p 在听" || echo "$p 未运行"
done
echo "=== us.log tail ==="
tail -3 /tmp/us.log 2>/dev/null
} > "$OUT" 2>&1
