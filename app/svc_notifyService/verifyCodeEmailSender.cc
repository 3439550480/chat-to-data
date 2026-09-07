#include "verifyCodeEmailSender.h"

namespace notifyService {

VerifyCodeEmailSender::VerifyCodeEmailSender(const mail_settings& settings)
    : EmailSender(settings) {
}

VerifyCodeEmailSender::~VerifyCodeEmailSender() {
}

std::stringstream VerifyCodeEmailSender::buildEmailBody(const std::string& to,
                                                        const std::string& subject,
                                                        const std::string& content) {
    // content 即验证码本身（NotifyBusiness 会把 code 传入 content）
    std::stringstream ss;
    ss << "Subject: " << subject << "\r\n";     // 邮件标题
    ss << "Content-Type: text/html\r\n";
    ss << "\r\n";                                // 头与正文之间的空行（MIME 格式要求）
    ss << "<html><body><p>你的验证码: <b>" << content << "</b></p>"
       << "<p>验证码将在5分钟后失效.</p></body></html>\r\n";
    return ss;
}

} // namespace notifyService
