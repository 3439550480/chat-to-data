#pragma once

#include <string>
#include <vector>
#include <cstddef>

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

    // ---------- 第 16 章增补：数据库 BLOB/UTF-8 辅助 ----------
    // 标准 Base64 编码（字母表 "+/"，带 '=' 填充）
    // 用途：MySQL/SQLite 的 BLOB 数据转文本（BLOB 可能含非法 UTF-8 字节，
    //       直塞 protobuf 字符串字段会因 proto3 的 UTF-8 校验失败，见 16 章第 35 页）
    static std::string base64Encode(const unsigned char* data, size_t len);
    // vector<char> 重载（驱动层直接传 BLOB 向量，课件调用处即此形态）
    static std::string base64Encode(const std::vector<char>& data);
    // 标准 Base64 解码（课件留 TODO，此处补全；非法输入返回空串）
    static std::string base64Decode(const std::string& str);
    // UTF-8 合法性校验（拒绝过短编码/代理对/超范围序列，课件 57-58 页实现）
    static bool isValidUtf8(const unsigned char* data, size_t len);
    // string 重载
    static bool isValidUtf8(const std::string& str);
};

} // end chat2Data
