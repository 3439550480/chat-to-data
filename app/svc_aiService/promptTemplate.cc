#include "promptTemplate.h"

namespace aiService {

PromptTemplate::PromptTemplate(const std::string& templateContent)
    : _template(templateContent) {
}

void PromptTemplate::setPlaceholder(const std::string& placeholder, const std::string& value) {
    _placeholders[placeholder] = value;
}

std::string PromptTemplate::build() const {
    std::string result = _template;
    for (const auto& pair : _placeholders) {
        // 1. 获取占位符和其对应的实际值（占位符形如 {name}）
        std::string placeholder = "{" + pair.first + "}";
        std::string value = pair.second;
        // 2. 循环替换占位符为其对应的实际值（同一占位符可能出现多次）
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    }
    // 3. 返回替换后的实际提示词
    return result;
}

} // namespace aiService
