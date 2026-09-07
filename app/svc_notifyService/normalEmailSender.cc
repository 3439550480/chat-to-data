#include "normalEmailSender.h"

namespace notifyService {

NormalEmailSender::NormalEmailSender(const mail_settings& settings)
    : EmailSender(settings) {
}

NormalEmailSender::~NormalEmailSender() {
}

std::stringstream NormalEmailSender::buildEmailBody(const std::string& to,
                                                    const std::string& subject,
                                                    const std::string& content) {
    std::stringstream ss;
    ss << "Subject: " << subject << "\r\n";
    ss << "Content-Type: text/html\r\n";
    ss << "\r\n";
    ss << content << "\r\n";    // 普通邮件：content 直接作为 HTML 正文
    return ss;
}

} // namespace notifyService
