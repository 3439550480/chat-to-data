#!/usr/bin/env bash
# ============================================================
# AI 服务段错误取证 round2：gdb backtrace 定位崩溃帧
# round1 结论：desc 空/非空两轮均 139，与 GLM_MODEL_DESC 无关，
#              且服务自身零输出 → 崩溃早于日志系统初始化
# ============================================================
cd /home/chat2Data || exit 1
export GLM_API_KEY="$GLM_API_KEY" GLM_MODEL_NAME="$GLM_MODEL_NAME" GLM_BASE_URL="$GLM_BASE_URL" GLM_MODEL_DESC="GLM-5.3-Flash"

echo "=== tools ==="
which gdb strace 2>/dev/null || echo "no gdb/strace"
echo "=== binary info ==="
file bin/AIService
echo "=== gdb backtrace ==="
if command -v gdb >/dev/null 2>&1; then
  gdb -batch -ex run -ex "bt 30" -ex "info registers rip rsp rdi rsi" --args ./bin/AIService --service_addr=dev-ai:9006 2>&1 | tail -70
else
  echo "NO GDB -> strace fallback"
  strace -f -o /tmp/ai.strace timeout 5 ./bin/AIService --service_addr=dev-ai:9006
  echo "EXIT: $?"
  tail -50 /tmp/ai.strace
fi
