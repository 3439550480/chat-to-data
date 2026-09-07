#include <curl/curl.h>
#include <bite_scaffold/log.h>
#include "notifyBusiness.h"

namespace notifyService {

NotifyBusiness::NotifyBusiness(const mail_settings& mailSettings) {
    // 1. 初始化 CURL 全局配置（进程级一次；cleanup 在析构中对称调用）
    auto ret = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (ret != CURLE_OK) {
        ERR("初始化CURL全局配置失败: {}", curl_easy_strerror(ret));
        abort();
    }
    // 2. 实例化验证码邮件 和 普通邮件 发送器对象
    _verifyCodeEmailSender = std::make_unique<VerifyCodeEmailSender>(mailSettings);
    _normalEmailSender = std::make_unique<NormalEmailSender>(mailSettings);
}

NotifyBusiness::~NotifyBusiness() {
    // 1. 停止发送邮件线程（排空队列 + join，之后成员析构才释放 sender）
    stop();
    // 2. 清理 CURL 全局配置
    curl_global_cleanup();
}

void NotifyBusiness::start() {
    _emailWorker.start();
    INF("NotifyBusiness started");
}

void NotifyBusiness::stop() {
    _emailWorker.stop();
    INF("NotifyBusiness stopped");
}

void NotifyBusiness::sendVerifyCodeEmail(const std::string& to, const std::string& code) {
    // 1. 构建验证码邮件任务（subject 复用为标题"验证码"，content 复用为验证码值）
    EmailTask task;
    task._toEmail = to;
    task._subject = "验证码";
    task._content = code;
    task._emailSender = _verifyCodeEmailSender.get();
    // 2. 任务入队（异步：入队即返回，由 EmailWorker 专职线程发送）
    _emailWorker.addTask(std::move(task));
    INF("添加验证码邮件任务: {}", to);
}

void NotifyBusiness::sendNormalEmail(const std::string& to,
                                     const std::string& subject,
                                     const std::string& content) {
    // 1. 构建普通邮件任务
    EmailTask task;
    task._toEmail = to;
    task._subject = subject;
    task._content = content;
    task._emailSender = _normalEmailSender.get();
    // 2. 任务入队
    _emailWorker.addTask(std::move(task));
    INF("添加普通邮件任务: {}", to);
}

} // namespace notifyService
