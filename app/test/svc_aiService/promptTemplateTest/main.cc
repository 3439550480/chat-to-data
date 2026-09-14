// H9 冒烟：三段提示词的占位符替换完整性（PromptTemplate + userPrompt.h）
// 验证点：所有实际使用的占位符都被替换，不残留 {xxx} 字面量给模型
#include <iostream>
#include <string>
#include <ai_chat_sdk/ChatSDK.h>          // 触发 SDK 头可用性检查
#include "userPrompt.h"
#include "promptTemplate.h"

static int g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) { std::cout << "[OK]   " << msg << std::endl; }               \
        else       { std::cout << "[FAIL] " << msg << std::endl; ++g_fail; }    \
    } while (0)

// 检查替换后是否残留指定占位符
static bool noLeftover(const std::string& built, const std::vector<std::string>& placeholders) {
    for (const auto& p : placeholders) {
        if (built.find("{" + p + "}") != std::string::npos) {
            std::cout << "       leftover: {" << p << "}" << std::endl;
            return false;
        }
    }
    return true;
}

int main() {
    // 1. 分析提示词：六个占位符全部替换（含课件漏替换的 display_type——AI9）
    {
        aiService::PromptTemplate p(aiService::ANALYSIS_PROMPT);
        p.setPlaceholder("DataBase", "MySQL");
        p.setPlaceholder("display_type", "Table/BarChart/ColumnChart/LineChart/"
                                         "AreaChart/PieChart/DonutChart/ScatterChart/NumberDisplay");
        p.setPlaceholder("table_schema", "【表结构】");
        p.setPlaceholder("table_name", "Sheet1_x");
        p.setPlaceholder("data_example", "【样例数据】");
        p.setPlaceholder("user_input", "统计各部门平均薪资");
        auto built = p.build();
        CHECK(noLeftover(built, {"DataBase", "table_schema", "table_name",
                                 "data_example", "user_input", "display_type"}),
              "analysis prompt: all placeholders replaced (AI9 display_type included)");
    }
    // 2. 总结提示词
    {
        aiService::PromptTemplate p(aiService::SUMMARY_PROMPT);
        p.setPlaceholder("user_input", "统计各部门平均薪资");
        p.setPlaceholder("result_json", "{\"columns\":[],\"rows\":[]}");
        auto built = p.build();
        CHECK(noLeftover(built, {"user_input", "result_json"}),
              "summary prompt: all placeholders replaced");
    }
    // 3. 邮件提示词
    {
        aiService::PromptTemplate p(aiService::EMAIL_PROMPT);
        p.setPlaceholder("email_param", "{}");
        p.setPlaceholder("user_input", "发送邮件");
        auto built = p.build();
        CHECK(noLeftover(built, {"email_param", "user_input"}),
              "email prompt: all placeholders replaced");
    }
    // 4. 结构完整性：三段提示词的关键标签与协议元素齐全
    CHECK(aiService::ANALYSIS_PROMPT.find("<TITLE_START>") != std::string::npos &&
          aiService::ANALYSIS_PROMPT.find("<TASKS_END>") != std::string::npos &&
          aiService::ANALYSIS_PROMPT.find("<ANALYSIS_START>") != std::string::npos &&
          aiService::ANALYSIS_PROMPT.find("<SQL_END>") != std::string::npos &&
          aiService::ANALYSIS_PROMPT.find("<EMAIL_START>sendEmail<EMAIL_END>") != std::string::npos,
          "analysis prompt: all stage tags present");
    CHECK(aiService::SUMMARY_PROMPT.find("taskStatus") != std::string::npos &&
          aiService::SUMMARY_PROMPT.find("chartConfig") != std::string::npos,
          "summary prompt: json fields present");
    CHECK(aiService::EMAIL_PROMPT.find("\"subject\"") != std::string::npos &&
          aiService::EMAIL_PROMPT.find("\"content\"") != std::string::npos,
          "email prompt: output fields present");

    std::cout << "summary: failCount=" << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
