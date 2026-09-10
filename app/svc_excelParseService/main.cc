#include <signal.h>          // 课件笔误：include 前缺 # 已修
#include <iostream>
#include <sstream>
#include <gflags/gflags.h>
#include <bite_scaffold/log.h>
#include "excelParserServer.h"

// RPC服务器配置
DEFINE_int32(port, 9003, "Excel解析服务RPC端口");
// ETCD配置
DEFINE_string(etcd_addr, "http://dev-etcd:2379", "ETCD地址");
// Excel解析子服务配置（课件问题㉖修复：192.168.150.129:9003 → 容器网络地址）
DEFINE_string(service_name, "ExcelParserService", "Excel解析子服务名称");
DEFINE_string(service_addr, "dev-env-service:9003", "Excel解析子服务地址");
// FastDFS配置（课件问题㉙修复：main.cc 从未调用 setFastDFSConfig——
// Builder 里的 int 是垃圾值；这里显式配置并闭合配置链。
// trackers 默认留空=占位启动：init 可过、真实下载等文件子服务章节部署后填入）
// B2 修正：默认值与 file/db 两个服务统一为 dev-tracker:22122（原为空 → 回退占位
// 127.0.0.1:22122 → 真实下载必失败，是"三服务默认值不一致"的启动陷阱）
DEFINE_string(fdfs_trackers, "dev-tracker:22122", "FastDFS tracker地址（逗号分隔，空=占位启动）");
DEFINE_int32(fdfs_connect_timeout, 30, "FastDFS连接超时（秒）");
DEFINE_int32(fdfs_network_timeout, 30, "FastDFS网络超时（秒）");
// 日志配置
DEFINE_bool(log_async, false, "是否启用异步日志");
DEFINE_int32(log_level, 2, "日志输出等级: 1-debug;2-info;3-warn;4-error;6-off");
DEFINE_string(log_format, "[%H:%M:%S][%-7l]: %v", "日志输出格式");
DEFINE_string(log_path, "stdout", "日志输出路径");

void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        exit(0);   // brpc 由 RunUntilAskedToQuit 管理，进程退出即释放
    }
}

int main(int argc, char* argv[]) {
    // 1. 解析命令行参数
    google::ParseCommandLineFlags(&argc, &argv, true);
    // 2. 初始化日志
    bitelog::log_settings logSettings;
    logSettings.async = FLAGS_log_async;
    logSettings.level = FLAGS_log_level;
    logSettings.format = FLAGS_log_format;
    logSettings.path = FLAGS_log_path;
    bitelog::bitelog_init(logSettings);
    INF("Bitelog initialized");
    // 3. 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    INF("Signal handlers registered");
    // 4. 构建注册中心配置对象
    excelParserService::RegisterCenterConfig registerCenterConfig;
    registerCenterConfig._etcdAddr = FLAGS_etcd_addr;
    registerCenterConfig._serviceName = FLAGS_service_name;
    registerCenterConfig._serviceAddr = FLAGS_service_addr;
    // 5. 构建FastDFS配置对象（按逗号拆分 tracker 列表）
    excelParserService::FastDFSConfig fastDFSConfig;
    if (!FLAGS_fdfs_trackers.empty()) {
        std::string item;
        std::istringstream iss(FLAGS_fdfs_trackers);
        while (std::getline(iss, item, ',')) {
            if (!item.empty()) {
                fastDFSConfig._trackers.push_back(item);
            }
        }
    }
    fastDFSConfig._connectTimeout = FLAGS_fdfs_connect_timeout;
    fastDFSConfig._networkTimeout = FLAGS_fdfs_network_timeout;
    if (fastDFSConfig._trackers.empty()) {
        WRN("FastDFS trackers 为空：占位启动，GetWorksheets/ParseExcel 的真实下载会失败");
    }
    // 6. 构建rpc服务器对象以及其他实例的创建
    auto rpcServer = excelParserService::ExcelParserServerBuilder()
        .setPort(FLAGS_port)
        .setRegisterCenterConfig(registerCenterConfig)
        .setFastDFSConfig(fastDFSConfig)
        .build();
    if (!rpcServer) {
        ERR("Failed to build ExcelParserServer");
        return -1;
    }
    // 7. 启动rpc服务器（阻塞直至退出信号）
    INF("ExcelParserService is running on port {}", FLAGS_port);
    rpcServer->start();
    return 0;
}
