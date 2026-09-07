#include <bite_scaffold/log.h>
#include "emailSender.h"

namespace notifyService {

EmailSender::EmailSender(const mail_settings& settings)
    : _settings(settings) {
}

EmailSender::~EmailSender() {
}

size_t EmailSender::callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    // 将邮件正文读取到 libcurl 指定的 buffer 中
    auto ss = static_cast<std::stringstream*>(userdata);
    ss->read(buffer, size * nitems);
    return ss->gcount();
}

bool EmailSender::sendEmail(const std::string& to,
                            const std::string& subject,
                            const std::string& content) {
    // 1. 初始化 curl 操作句柄
    auto curl = curl_easy_init();
    if (curl == nullptr) {
        ERR("构造CURL操作句柄失败!");
        return false;
    }
    // 2. 设置超时参数（连接超时15秒，总超时30秒）
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    // 3. SSL认证参数——开发阶段不认证（生产环境应启用证书校验）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // 3.5 强制要求 TLS（课件问题㉔）：smtps:// 已是 implicit TLS（此断言自然满足）；
    //     smtp:// + 587 则触发 STARTTLS 明文升级，防止授权码明文传输
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    // 4. SMTP服务器地址（scheme 校验：防止 smtp:// 误连 465 implicit TLS 端口
    //    ——明文 EHLO 撞上等 ClientHello 的服务器会被直接断连，报错毫无指向性）
    if (_settings._url.rfind("smtp://", 0) == 0 &&
        _settings._url.find(":465") != std::string::npos) {
        ERR("邮箱配置错误: 465端口必须用 smtps:// 前缀（implicit TLS），当前URL: {}",
            _settings._url);
        curl_easy_cleanup(curl);
        return false;
    }
    auto ret = curl_easy_setopt(curl, CURLOPT_URL, _settings._url.c_str());
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_URL失败: {}", curl_easy_strerror(ret));
        curl_easy_cleanup(curl);
        return false;
    }
    // 5. 邮箱用户名（即邮箱号）
    ret = curl_easy_setopt(curl, CURLOPT_USERNAME, _settings._username.c_str());
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_USERNAME失败: {}", curl_easy_strerror(ret));
        curl_easy_cleanup(curl);
        return false;
    }
    // 6. 授权码
    ret = curl_easy_setopt(curl, CURLOPT_PASSWORD, _settings._password.c_str());
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_PASSWORD失败: {}", curl_easy_strerror(ret));
        curl_easy_cleanup(curl);
        return false;
    }
    // 7. 发件人邮箱
    ret = curl_easy_setopt(curl, CURLOPT_MAIL_FROM, _settings._from.c_str());
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_MAIL_FROM失败: {}", curl_easy_strerror(ret));
        curl_easy_cleanup(curl);
        return false;
    }
    // 8. 收件人邮箱（本项目只发给单个人，收件人链表设置一个即可）
    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, to.c_str());
    ret = curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_MAIL_RCPT失败: {}", curl_easy_strerror(ret));
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return false;
    }
    // 9. 构建邮件正文（多态：由子类决定格式）
    std::stringstream ss = buildEmailBody(to, subject, content);
    if (ss.str().empty()) {
        ERR("构建邮件正文失败!");
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return false;
    }
    // 10. 设置邮件正文数据源
    ret = curl_easy_setopt(curl, CURLOPT_READDATA, &ss);
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_READDATA失败: {}", curl_easy_strerror(ret));
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return false;
    }
    // 11. 设置正文读取回调
    ret = curl_easy_setopt(curl, CURLOPT_READFUNCTION, &EmailSender::callback);
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_READFUNCTION失败: {}", curl_easy_strerror(ret));
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return false;
    }
    // 12. 启用上传模式（SMTP协议要求）
    ret = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    if (ret != CURLE_OK) {
        ERR("设置CURLOPT_UPLOAD失败: {}", curl_easy_strerror(ret));
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return false;
    }
    // 13. 执行完整SMTP通信（连接、认证、发送）
    ret = curl_easy_perform(curl);
    if (ret != CURLE_OK) {
        ERR("请求邮件服务器失败: {}", curl_easy_strerror(ret));
        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
        return false;
    }
    // 14. 释放资源
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    INF("发送邮件成功: to={}, subject={}", to, subject);
    return true;
}

} // namespace notifyService
