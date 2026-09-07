#pragma once
#include <sstream>
#include "emailSender.h"

namespace notifyService {

// 验证码邮件发送器
class VerifyCodeEmailSender : public EmailSender {
public:
    explicit VerifyCodeEmailSender(const mail_settings& settings);
    ~VerifyCodeEmailSender() override;
    // 构造验证码邮件正文（课件笔误"验证码有家正文"已修正）
    std::stringstream buildEmailBody(const std::string& to,
                                     const std::string& subject,
                                     const std::string& content) override;
};

} // namespace notifyService
