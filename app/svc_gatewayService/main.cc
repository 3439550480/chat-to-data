#include <signal.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <gflags/gflags.h>
#include <bite_scaffold/log.h>
#include "gatewayServer.h"

// 配置文件路径
DEFINE_string(conf, "chat2Data.conf", "配置文件路径");

// 网关服务配置
DEFINE_int32(listen_port, 0, "网关服务器端口");

// ETCD地址
DEFINE_string(etcd_addr, "", "ETCD地址");

// 其他子服务名称配置（用于RPC调用）
DEFINE_string(user_service, "", "用户子服务名称");
DEFINE_string(file_service, "", "文件子服务名称");
DEFINE_string(db_service, "", "数据库子服务名称");
DEFINE_string(ai_service, "", "AI子服务名称");

// 日志配置
DEFINE_bool(log_async, false, "是否启用异步日志");
DEFINE_int32(log_level, 2, "日志输出等级: 1-debug;2-info;3-warn;4-error;6-off");
DEFINE_string(log_format, "", "日志输出格式");
// 默认值对齐脚手架 bitelog 的 log_settings.path 默认值 "stdout"（课件原版为 ""，
// 空串会让 spdlog 尝试打开空文件路径直接抛异常——见 CODING_SPEC 规则3 偏差记录）
DEFINE_string(log_path, "stdout", "日志输出路径");

static std::shared_ptr<GatewayService::GatewayServer> g_server = nullptr;

// 信号处理函数, 当接收到中断或终止信号时，安全停止网关服务器
void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        INF("Received signal {}, starting cleanup...", signum);
        if (g_server && g_server->isRunning()) {
            g_server->stop();
            INF("GatewayServer stopped by signal handler");
        }
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    // 步骤1：解析命令行参数
    // gflags会解析--port、--etcd_addr等参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    // 设置配置文件路径，从命令行参数获取（必须在 ParseCommandLineFlags 之后，否则 --conf 还没被解析）
    gflags::SetCommandLineOption("flagfile", FLAGS_conf.c_str());

    // 步骤2：配置并初始化日志系统
    bitelog::log_settings logSettings;
    logSettings.async = FLAGS_log_async;
    logSettings.level = FLAGS_log_level;
    logSettings.format = FLAGS_log_format;
    logSettings.path = FLAGS_log_path;
    bitelog::bitelog_init(logSettings);
    INF("Bitelog initialized");

    // 步骤3：注册信号处理器
    // 捕获SIGINT（Ctrl+C）和SIGTERM（kill命令）信号，确保服务器能够优雅地停止
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    INF("Signal handlers registered");

    // 步骤4：构建网关服务器（链式装配：地址 → 端口 → ETCD → 要发现的服务名列表）
    g_server = GatewayService::GatewayServerBuilder()
        .setHost("0.0.0.0")
        .setPort(FLAGS_listen_port)
        .setEtcdAddr(FLAGS_etcd_addr)
        .addService(FLAGS_user_service)
        .addService(FLAGS_file_service)
        .addService(FLAGS_db_service)
        .addService(FLAGS_ai_service)
        .build();

    INF("port: {}", FLAGS_listen_port);

    // 步骤5：检查服务器是否构建成功
    if (!g_server) {
        ERR("Failed to build GatewayServer");
        return -1;
    }

    INF("GatewayServer is running on port {}", FLAGS_listen_port);

    // 步骤6：主线程进入等待循环（每秒检查一次运行状态）
    // 监听线程是 detached 的，主线程若退出进程就没了；轮询保证能在服务器异常停止时正常退出
    while (true) {
        if (!g_server->isRunning()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    INF("GatewayServer main loop exited");
    return 0;
}
