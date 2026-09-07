#include <signal.h>
#include <iostream>
#include <gflags/gflags.h>
#include <bite_scaffold/log.h>
#include "notifyServer.h"

// RPC服务器配置
DEFINE_int32(port, 9002, "通知服务RPC端口");
// ETCD配置
DEFINE_string(etcd_addr, "http://dev-etcd:2379", "ETCD地址");
// 通知子服务配置（注册地址用容器网络名，网关经 dev-network 可达）
// 课件问题⑳修复：原版 192.168.150.129:9002 → 容器网络 dev-env-service:9002
DEFINE_string(service_name, "NotifyService", "通知子服务名称");
DEFINE_string(service_addr, "dev-env-service:9002", "通知子服务地址");
// 邮箱配置
// 课件问题㉑处理：课件将真实邮箱账号和授权码明文印在PDF中（已泄露），
// 此处只留占位符——运行前通过命令行或此处填入你自己的账号与SMTP授权码
DEFINE_string(mail_username, "", "邮箱用户名（即邮箱号）");
DEFINE_string(mail_password, "", "SMTP授权码（非登录密码，邮箱后台生成）");
DEFINE_string(mail_url, "smtps://smtp.163.com:465", "SMTP服务器地址（465端口必须用smtps://）");
DEFINE_string(mail_from, "", "发件人邮箱");
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
    // 4. 邮箱配置防御性检查（授权码为空时启动即提醒，避免联调时排查半天）
    if (FLAGS_mail_username.empty() || FLAGS_mail_password.empty() || FLAGS_mail_from.empty()) {
        WRN("邮箱配置不完整(username/password/from)，服务可启动但真实发信会失败:"
            "启动时加 -mail_username=... -mail_password=... -mail_from=...");
    }
    // 5. 构建邮箱配置对象
    notifyService::mail_settings mailConfig;
    mailConfig._username = FLAGS_mail_username;
    mailConfig._password = FLAGS_mail_password;
    mailConfig._url = FLAGS_mail_url;
    mailConfig._from = FLAGS_mail_from;
    // 6. 构建注册中心配置对象
    notifyService::registerCenterConfig registerCenterConfig;
    registerCenterConfig._etcdAddr = FLAGS_etcd_addr;
    registerCenterConfig._serviceName = FLAGS_service_name;
    registerCenterConfig._serviceAddr = FLAGS_service_addr;
    // 7. 构建rpc服务器对象以及其他实例的创建
    auto rpcServer = notifyService::NotifyServerBuilder()
        .setPort(FLAGS_port)
        .setMailConfig(mailConfig)
        .setRegisterCenterConfig(registerCenterConfig)
        .build();
    if (!rpcServer) {
        ERR("Failed to build NotifyServer");
        return -1;
    }
    // 8. 启动rpc服务器（阻塞直至退出信号）
    INF("NotifyService is running on port {}", FLAGS_port);
    rpcServer->start();
    return 0;
}
