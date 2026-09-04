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

} // end chat2Data
