#!/usr/bin/env bash
# ============================================================
# Chat2Data 部署容器入口脚本（本地 Docker Desktop 方案，P2）
#
# 与官方课件 entrypoint.sh 的差异（21 章甄别结论 D1 系列）：
#   官方：envsubst < ${CONF_NAME} > /home/chat2Data/chat2Data.conf && exec ${BIN_NAME}
#         —— 官方 7 个服务共用统一 conf 模板，靠 envsubst 把 compose 环境变量
#            （LISTEN_PORT / SVC_* 地址）注入 conf，因此需要 envsubst 工具。
#   我们：仅网关有 conf 机制（DEFINE_string(conf,"chat2Data.conf") + flagfile），
#         conf 已随镜像 COPY 并就位于 CWD（见 Dockerfile 的 RUN mv）；
#         6 个子服务无 conf flag、默认值自包含（dev-mysql / dev-redis / dev-etcd /
#         dev-tracker 与 bite-dev-environment compose 服务名精确对齐），
#         无需 envsubst、无需统一 conf。
#   子服务 ETCD 注册地址必须逐容器覆盖 → 由 compose 通过 EXTRA_FLAGS
#   传 --service_addr=dev-xxx:900x（本容器网络名，网关按注册地址转发）。
#
# 注意：EXTRA_FLAGS 故意不加引号 —— word splitting 在这里是期望行为
#（多个 flag 按空格拆开逐个传给二进制；加了引号反而会被当成单个参数）。
# ============================================================
set -e

# BIN_NAME 必须由 compose 注入（如 bin/GatewayService），缺失立即报错退出
: "${BIN_NAME:?environment BIN_NAME is required (e.g. bin/GatewayService)}"

echo "[entrypoint] exec ${BIN_NAME} ${EXTRA_FLAGS}"
exec "${BIN_NAME}" ${EXTRA_FLAGS}
