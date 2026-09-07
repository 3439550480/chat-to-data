#pragma once

#include <string>
#include "emailSender.h"

namespace notifyService {

// 邮件发送任务（纯数据结构，由业务层组装、EmailWorker 消费）
struct EmailTask {
    std::string _toEmail;         // 收件人邮箱
    std::string _subject;         // 邮件主题
    std::string _content;         // 邮件内容（验证码邮件=验证码值；普通邮件=HTML正文）
    EmailSender* _emailSender;    // 邮件发送器指针（多态调用 buildEmailBody）
};

} // namespace notifyService
