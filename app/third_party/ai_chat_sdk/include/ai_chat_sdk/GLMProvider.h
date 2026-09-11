#pragma once
#include "LLMProvider.h"

#include <functional>
#include <string>
#include <map>
#include <vector>
#include "common.h"

namespace ai_chat_sdk{
    // ===== 本项目补丁（PATCH 2）：GLM（及任意 OpenAI 兼容服务）Provider =====
    // 与 DeepSeekProvider 的差异：
    //   ① 模型名不再硬编码，来自 initModel 的 model_name；
    //   ② baseUrl 来自 initModel 的 endpoint（支持带路径前缀，如
    //      https://open.bigmodel.cn/api/paas/v4）；
    //   ③ 协议与 DeepSeek 相同（OpenAI 兼容 POST {baseUrl}/chat/completions + SSE delta）
    class GLMProvider : public LLMProvider{
    public:
        // 初始化模型：读取 api_key / endpoint / model_name / model_desc
        virtual bool initModel(const std::map<std::string, std::string>& modelConfig);
        // 检测模型是否有效
        virtual bool isAvailable() const;
        // 获取模型名称（来自配置）
        virtual std::string getModelName() const;
        // 获取模型描述（来自配置）
        virtual std::string getModelDesc() const;
        // 发送消息 - 全量返回
        virtual std::string sendMessage(const std::vector<Message>& messages,
                                        const std::map<std::string, std::string>& requestParam);
        // 发送消息 - 增量返回（流式）
        virtual std::string sendMessageStream(const std::vector<Message>& messages,
                                              const std::map<std::string, std::string>& requestParam,
                                              std::function<void(const std::string&, bool)> callback);

        // 内部辅助：请求路径（basePath + /chat/completions 或用户已给完整路径）
        static std::string buildRequestPath(const std::string& basePath);

    private:
        std::string _modelName;    // 模型名称（配置传入）
        std::string _modelDesc;    // 模型描述（配置传入）
    };
} // end ai_chat_sdk
