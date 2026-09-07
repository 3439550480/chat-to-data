#pragma once
#include <memory>
#include <string>
#include "emailSender.h"
#include "verifyCodeEmailSender.h"
#include "normalEmailSender.h"
#include "emailWorker.h"

namespace notifyService {

// 邮件通知业务层：组装邮件发送器与异步工作线程，
// 对 RPC 层提供"入队即返回"的两个发送入口
class NotifyBusiness {
public:
    explicit NotifyBusiness(const mail_settings& mailSettings);
    ~NotifyBusiness();
    // 启动邮件发送线程
    void start();
    // 停止邮件发送线程
    void stop();
    // 发送验证码邮件（异步：组装任务入队即返回）
    void sendVerifyCodeEmail(const std::string& to, const std::string& code);
    // 发送普通邮件（异步：组装任务入队即返回）
    void sendNormalEmail(const std::string& to,
                         const std::string& subject,
                         const std::string& content);
private:
    // 成员声明顺序即析构安全的关键：_emailWorker 声明在最后，
    // 析构时最先析构（先 join 工作线程），之后才释放两个 sender
    // ——保证 EmailTask 裸指针在任务执行期间永远有效
    std::unique_ptr<VerifyCodeEmailSender> _verifyCodeEmailSender;  // 验证码邮件发送器
    std::unique_ptr<NormalEmailSender> _normalEmailSender;          // 普通邮件发送器
    EmailWorker _emailWorker;                                       // 异步发送线程
};

} // namespace notifyService
