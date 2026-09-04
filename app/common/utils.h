#pragma once

#include <string>
#include <vector>

namespace chat2Data{

// 工具类，定义各种工具函数
class Utils{
public:
    // UUID 生成器
    static std::string generateUuid();

    // bcrypt 盐值专用 base64 编码（crypt $2b$ 格式要求）
    // ⚠️ 与标准 base64 不同：字母表为 "./A-Za-z0-9"（无 +/=），位序为低 6 位优先
    // 16 字节随机盐 → 22 字符盐串，供 crypt_r 的 "$2b$10$" 前缀使用
    static std::string bcryptSaltEncode(const std::vector<char>& input);
};

} // end chat2Data
