#!/bin/bash
# G21-P2：收拢部署产物 → deploy/bin/
# 在开发容器内执行：bash /home/dev/chat2data/deploy/collect.sh
# 产出：deploy/bin/{7 个子服务二进制, chat2Data.conf, www/}
# 说明：这是 21 章统一构建（根 CMakeLists）的替代方案——我们各服务
# 独立 CMake + bite_scaffold 传递依赖体系不动，只把现成产物集中收拢，
# 供 Dockerfile 的 COPY bin/ 使用（构建上下文 = deploy/）
set -e
ROOT=/home/dev/chat2data
DEPLOY=$ROOT/deploy
OUT=$DEPLOY/bin

rm -rf "$OUT"
mkdir -p "$OUT"

# 1. 七个子服务二进制（svc_xxx/build/<Name>）
for pair in \
  "svc_userService/UserService" \
  "svc_notifyService/NotifyService" \
  "svc_excelParseService/ExcelParserService" \
  "svc_fileService/FileService" \
  "svc_dbService/DatabaseService" \
  "svc_aiService/AIService" \
  "svc_gatewayService/GatewayService"; do
  dir=${pair%%/*}; bin=${pair##*/}
  cp "$ROOT/$dir/build/$bin" "$OUT/"
  echo "collect: $bin"
done

# 2. 网关 conf（G-H5 修正版：服务名驼峰 + listen_port=9000）
#    网关 main.cc 有 FLAGS_conf 机制（SetCommandLineOption flagfile），
#    容器内 CWD=/home/chat2Data，程序启动自动读本文件；6 个子服务无
#    conf flag，不会读它，共用目录无冲突
cp "$ROOT/svc_gatewayService/build/chat2Data.conf" "$OUT/"

# 3. 前端（deploy/www 修正版 → bin/www，与网关可执行文件同目录，
#    匹配 gatewayServiceImpl 构造函数的 _exePath + "/www" 约定）
cp -r "$DEPLOY/www" "$OUT/www"

echo "=== 产物清单 ==="
ls -lh "$OUT"
