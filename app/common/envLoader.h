#pragma once
#include <string>

namespace chat2Data {

// 极简 .env 文件加载器（第 18 章 AI 子服务用；不引第三方库）
// 约定：
//   · 每行 KEY=VALUE；跳过空行与以 # 开头的注释行
//   · 值两端的成对单/双引号会被剥离；# 之后的内容视为行内注释（需前置空格）
//   · 已存在的同名环境变量【不覆盖】（真实环境变量优先于 .env）
// @param path .env 文件路径
// @return 成功读取并设置至少一个变量返回 true；文件不存在返回 false
bool loadEnvFile(const std::string& path);

// 读取环境变量，缺失或为空时返回默认值
std::string getEnvOrDefault(const std::string& key, const std::string& defaultValue = "");

} // namespace chat2Data
