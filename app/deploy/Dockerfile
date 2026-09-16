# ============================================================
# Chat2Data 部署镜像 —— A 档（FROM 现有 dev-environment:1.0，不做 ldd 瘦身）
#
# 甄别结论（21 章 vs 本地 Docker Desktop）：
#   官方课件在云服务器上 FROM ubuntu + 导出依赖 so + dependLib + ld.so.conf 注册，
#   是为了产出一个最小可分发镜像。
#   我们本地构建：dev-environment:1.0 内已预编译依赖（brpc / ODB / OpenXLSX /
#   fastdfs / leveldb / jsoncpp 等）且自带 curl，直接复用，整套依赖流程全部免除。
#
# 前置：先在容器内跑 deploy/collect.sh 生成 bin/（7 二进制 + 网关 conf + www）。
# 构建（宿主 PowerShell，在 app/ 下执行）：
#   docker build -t chat2data:1.0 deploy/
# ============================================================
FROM crpi-l2mxiaa0yapq1c5e.cn-chengdu.personal.cr.aliyuncs.com/bite-dev-environment/dev-environment:1.0

# CWD = /home/chat2Data：网关 conf 机制按 CWD 直读 chat2Data.conf，
# 子服务 / 网关二进制均以相对路径 bin/XxxService 由 entrypoint exec。
WORKDIR /home/chat2Data

# 收拢产物：bin/（7 二进制 + chat2Data.conf + www/）+ 入口脚本
COPY bin/ ./bin/
COPY entrypoint.sh ./entrypoint.sh

# 网关 conf 机制（DEFINE_string(conf,"chat2Data.conf")）按 CWD 解析相对路径；
# collect.sh 把 conf 收拢进 bin/，这里挪到 CWD —— 这样 compose 里网关的
# EXTRA_FLAGS 可保持为空，conf 原样生效（listen_port=9000 等已实测）。
RUN chmod +x ./entrypoint.sh \
 && mv ./bin/chat2Data.conf ./chat2Data.conf

ENTRYPOINT ["/home/chat2Data/entrypoint.sh"]
