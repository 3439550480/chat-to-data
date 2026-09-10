#include <random>
#include <atomic>
#include <sstream>
#include <iomanip>
#include "utils.h"

namespace chat2Data{

// uuid生成器
static std::atomic<uint16_t> uuidCounter{0};

std::string Utils::generateUuid() {
    // 生成一个由16位随机字符组成的字符串作为唯一ID
    // thread_local：随机数引擎非线程安全，每个线程持有独立引擎，避免加锁
    thread_local static std::random_device rd;
    thread_local static std::mt19937 gen(rd());
    thread_local static std::uniform_int_distribution<int> dis(0, 255);

    // 1. 生成6个0~255之间的随机数字(1字节-转换为16进制字符)--生成12位16进制字符
    uint8_t bytes[6];
    for (int i = 0; i < 6; ++i) {
        bytes[i] = static_cast<uint8_t>(dis(gen));
    }

    // 2. 生成计数（atomic 保证多线程下不重复，是唯一性的第二道保险）
    uint16_t counter = uuidCounter.fetch_add(1);

    // 3. 构建uuid， 格式：038d-838e161f-0001
    // setw(2)+setfill('0')：保证字节值小于16时补前导0（0x0a 输出 0a 而非 a），否则长度不固定
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(2) << static_cast<int>(bytes[0])
        << std::setw(2) << static_cast<int>(bytes[1])
        << '-'
        << std::setw(2) << static_cast<int>(bytes[2])
        << std::setw(2) << static_cast<int>(bytes[3])
        << std::setw(2) << static_cast<int>(bytes[4])
        << std::setw(2) << static_cast<int>(bytes[5])
        << '-'
        << std::setw(4) << static_cast<int>(counter);

    return oss.str();
}

std::string Utils::bcryptSaltEncode(const std::vector<char>& input) {
    // bcrypt 专用字母表（OpenBSD crypt）：'.'、'/' 在前，字母数字在后，无 '+'/'='
    // 位序与标准 base64 相反：每输出 1 个字符取 value 的低 6 位
    static const char B64T[] =
        "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    std::string result;
    uint32_t value = 0;
    size_t nbits = 0;
    for (char c : input) {
        // 按字节累积：新字节放高位，每次凑满 6 bits 就吐出一个字符（取低 6 位）
        // 先转 unsigned char 再左移：有符号 char 最高位为 1 时会符号扩展污染 value
        value |= static_cast<unsigned char>(c) << nbits;
        nbits += 8;
        while (nbits >= 6) {
            result += B64T[value & 0x3f];
            value >>= 6;
            nbits -= 6;
        }
    }
    // 剩余不足 6 bits 的位（16 字节 = 128 bits → 最后剩 2 bits）截断输出最后一个字符
    if (nbits) {
        result += B64T[value & ((1 << nbits) - 1)];
    }
    return result;   // 16 字节输入 → 恒定 22 字符输出
}

// ==================== 第 16 章增补：Base64 编解码 + UTF-8 校验 ====================

// 标准 Base64 编码（课件 54-56 页实现，注释保留其位运算说明）
std::string Utils::base64Encode(const unsigned char* data, size_t len) {
    // Base64字符表：64个可打印字符
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string ret;
    ret.reserve((len + 2) / 3 * 4);   // 预估输出长度，避免反复扩容
    int i = 0;
    unsigned char char_array_3[3];    // 存储3个输入字节
    unsigned char char_array_4[4];    // 存储4个输出字符的索引值
    // 循环处理：每3个字节编码为4个字符
    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            // 将3个字节(24个比特位)重新分组为4个6位值
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; i < 4; i++) {
                ret += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    // 处理剩余字节(不足3个)：补0对齐后只输出有效字符，'=' 填充
    if (i > 0) {
        for (int j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (int j = 0; j < i + 1; j++) {
            ret += base64_chars[char_array_4[j]];
        }
        while (i++ < 3) {
            ret += '=';
        }
    }
    return ret;
}

// vector<char> 重载（课件 mysqlDatabase/sqliteDatabase 的实际调用形态）
std::string Utils::base64Encode(const std::vector<char>& data) {
    if (data.empty()) { return ""; }
    return base64Encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

// 标准 Base64 解码（课件留 TODO，此处补全；非法字符/非法长度返回空串）
std::string Utils::base64Decode(const std::string& str) {
    // 反查表：字符 → 6位索引，'=' 之外的非法字符记 -1
    static int decodeTable[256];
    static bool tableInit = false;
    if (!tableInit) {
        // C++11 起函数内 static 初始化是线程安全的（魔法静态）
        for (int i = 0; i < 256; ++i) { decodeTable[i] = -1; }
        const char* chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
        for (int i = 0; i < 64; ++i) {
            decodeTable[static_cast<unsigned char>(chars[i])] = i;
        }
        tableInit = true;
    }
    std::string ret;
    ret.reserve(str.length() / 4 * 3);
    int val = 0;       // 累积的 6 位组
    int valb = -8;     // 已累积位数（负值表示还不够一个字节）
    for (unsigned char c : str) {
        if (c == '=') { break; }          // 填充符之后不再有数据
        int d = decodeTable[c];
        if (d == -1) { return ""; }       // 非法字符 → 整串拒绝
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            ret += static_cast<char>((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return ret;
}

// UTF-8 合法性校验（课件 56-58 页实现：拒绝过短编码/代理对/超范围序列）
bool Utils::isValidUtf8(const unsigned char* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned char c = data[i];
        // 1. 单字节 ASCII (0xxxxxxx): U+0000 ~ U+007F
        if (c <= 0x7F) {
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2. 双字节字符 (110xxxxx 10xxxxxx): U+0080 ~ U+07FF
            if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            // [安全性] 拒绝过短编码：0xC0/0xC1 开头总是过短编码（如 0xC0 0x80 应为 0x00）
            if (c < 0xC2) {
                return false;
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3. 三字节字符 (1110xxxx 10xxxxxx 10xxxxxx): U+0800 ~ U+FFFF
            if (i + 2 >= len) {
                return false;
            }
            unsigned char c1 = data[i + 1];
            unsigned char c2 = data[i + 2];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
                return false;
            }
            // [安全性] 拒绝过短编码：0xE0 时第二字节必须 >= 0xA0
            if (c == 0xE0 && c1 < 0xA0) {
                return false;
            }
            // [标准合规] 拒绝 UTF-16 代理对（U+D800~U+DFFF 不应出现在 UTF-8 中）
            if (c == 0xED && c1 >= 0xA0) {
                return false;
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4. 四字节字符 (11110xxx 10xxxxxx ×3): U+10000 ~ U+10FFFF
            if (i + 3 >= len) {
                return false;
            }
            unsigned char c1 = data[i + 1];
            unsigned char c2 = data[i + 2];
            unsigned char c3 = data[i + 3];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                return false;
            }
            // [安全性] 拒绝过短编码：0xF0 时第二字节必须 >= 0x90
            if (c == 0xF0 && c1 < 0x90) {
                return false;
            }
            // [标准合规] 拒绝超出 Unicode 最大码点：最大合法序列 0xF4 0x8F 0xBF 0xBF
            if (c > 0xF4) {
                return false;
            }
            if (c == 0xF4 && c1 > 0x8F) {
                return false;
            }
            i += 4;
        } else {
            // 5. 非法起始字节（孤立延续字节 / 0xC0~0xC1 兜底 / 0xF5~0xFF 超范围）
            return false;
        }
    }
    return true;
}

// string 重载
bool Utils::isValidUtf8(const std::string& str) {
    return isValidUtf8(reinterpret_cast<const unsigned char*>(str.data()), str.size());
}

} // end chat2Data
