#pragma once
#include <string>
#include <unordered_map>

namespace aiService {
// 提示词模板类：占位符形如 {name}，setPlaceholder 记录映射，build 循环替换
class PromptTemplate {
public:
    // 使用提示词模板构建对象
    explicit PromptTemplate(const std::string& templateContent);
    // 设置占位符和实际值的映射关系
    void setPlaceholder(const std::string& placeholder, const std::string& value);
    // 获取替换后的实际提示词
    std::string build() const;
private:
    std::string _template;                                      // 提示词模板
    std::unordered_map<std::string, std::string> _placeholders; // 占位符映射
};
} // namespace aiService
