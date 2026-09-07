#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <curl/curl.h>

namespace notifyService {

// 邮箱服务器配置结构
struct mail_settings {
    std::string _username;    // 邮箱用户名（即邮箱号）
    std::string _password;    // SMTP授权码（非登录密码）
    std::string _url;         // SMTP服务器地址（如 smtps://smtp.163.com:465）
    std::string _from;        // 发件人邮箱
};

// 邮件发送抽象基类：封装 libcurl 发送邮件的完整流程，
// 子类只需实现 buildEmailBody 完成不同邮件的正文构造
class EmailSender {
public:
    explicit EmailSender(const mail_settings& settings);
    virtual ~EmailSender();
    // 构建邮件正文（纯虚函数，由子类实现不同格式）
    virtual std::stringstream buildEmailBody(const std::string& to,
                                             const std::string& subject,
                                             const std::string& content) = 0;
    // 发送邮件（libcurl/SMTP 完整流程，14 步）
    bool sendEmail(const std::string& to,
                   const std::string& subject,
                   const std::string& content);
private:
    // libcurl 数据读取回调：把邮件正文从 userdata(ss) 读入 curl 的缓冲区
    static size_t callback(char* buffer, size_t size, size_t nitems, void* userdata);
protected:
    mail_settings _settings;    // 邮箱配置
};

} // namespace notifyService
