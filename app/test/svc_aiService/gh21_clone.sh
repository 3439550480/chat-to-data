#!/bin/bash
# G21-P2 前置：容器内浅克隆官方仓库，取 www 前端 + 对照官方部署结构
OUT=/home/dev/chat2data/tmp/gh21_clone.txt
{
which git || echo "no git in container"
rm -rf /home/dev/chat2data/tmp/chat2data-tech
git clone --depth 1 https://gitee.com/zhibite-edu/chat2data-tech.git /home/dev/chat2data/tmp/chat2data-tech 2>&1 | tail -3
echo "=== 仓库顶层 ==="
ls /home/dev/chat2data/tmp/chat2data-tech/ 2>&1
echo "=== chat2Data/ ==="
ls /home/dev/chat2data/tmp/chat2data-tech/chat2Data/ 2>&1
echo "=== chat2Data/bin/ ==="
ls /home/dev/chat2data/tmp/chat2data-tech/chat2Data/bin/ 2>&1 | head -25
echo "=== chat2Data/bin/www/ ==="
ls /home/dev/chat2data/tmp/chat2data-tech/chat2Data/bin/www/ 2>&1 | head -30
} > "$OUT" 2>&1
