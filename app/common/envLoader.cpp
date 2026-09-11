#include "envLoader.h"
#include <cstdlib>
#include <unistd.h>      // setenv
#include <fstream>
#include <algorithm>
#include <cctype>
#include <bite_scaffold/log.h>

namespace chat2Data {

namespace {
// 去掉首尾空白
std::string trim(const std::string& s) {
    auto begin = std::find_if_not(s.begin(), s.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}
} // namespace

bool loadEnvFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;   // 无 .env 属正常情况（可回落到真实环境变量）
    }
    int loadedCount = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        // 1. 去掉行尾 \r（Windows 编辑的文件）
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string trimmed = trim(line);
        // 2. 跳过空行与注释行
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        // 3. 切分 KEY=VALUE
        size_t eqPos = trimmed.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }
        std::string key = trim(trimmed.substr(0, eqPos));
        std::string value = trim(trimmed.substr(eqPos + 1));
        if (key.empty()) {
            continue;
        }
        // 4. 剥离成对引号
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        // 5. 写入环境变量（不覆盖已存在的真实环境变量）
        if (setenv(key.c_str(), value.c_str(), 0) == 0) {
            ++loadedCount;
        }
    }
    INF("Env file loaded: path={}, variables={}", path, loadedCount);
    return loadedCount > 0;
}

std::string getEnvOrDefault(const std::string& key, const std::string& defaultValue) {
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || value[0] == '\0') {
        return defaultValue;
    }
    return std::string(value);
}

} // namespace chat2Data
