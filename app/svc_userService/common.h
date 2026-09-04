#pragma once

#include <string>
#include <cstdint>

namespace userService {

enum class UserStatus {
    Offline = 0,    // 离线
    Online = 1      // 在线

};

struct UserInfo {
    std::string _userId;     // 用户Id
    std::string _nickname;   // 用户昵称
    std::string _password;   // 用户密码
    std::string _email;      // 用户邮箱
    UserStatus _status;      // 用户状态：在线、离线
};

struct SessionInfo {
    std::string _sessionId;  // 会话Id
    std::string _userId;     // 用户Id
};

struct VerifyCodeInfo {
    std::string _codeId;      // 验证码Id
    std::string _verifyCode;  // 验证码
    std::string _email;       // 验证码邮箱
    std::string _createTime;  // 验证码创建时间
};

struct MysqlConfig {
    std::string _host;      // MySQL主机地址
    std::string _user;      // MySQL用户名
    std::string _passwd;    // MySQL密码
    std::string _db;        // MySQL数据库名称
    unsigned int _port;     // MySQL端口号
    std::string _cset;      // MySQL字符集
    unsigned int _connectionPoolSize;  // MySQL连接池大小
};

struct RedisConfig {
    std::string _host;      // Redis主机地址
    int _port;              // Redis端口号
    std::string _passwd;    // Redis密码
    int _db;                // Redis数据库索引--默认索引为0
    size_t _connectionPoolSize;  // Redis连接池大小
};

} // namespace userService
