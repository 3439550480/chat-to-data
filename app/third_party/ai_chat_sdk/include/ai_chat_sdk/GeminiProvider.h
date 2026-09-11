#pragma once
#include "LLMProvider.h"


namespace ai_chat_sdk{
    // LLMProvider 类
    class GeminiProvider : public LLMProvider{
    public:
        // 初始化模型
        virtual bool initModel(const std::map<std::string, std::string>& modelConfig) override;
        // 检测模型是否有效
        virtual bool isAvailable() const override;
        // 获取模型名称
        virtual std::string getModelName() const override;
        // 获取模型描述
        virtual std::string getModelDesc() const override;
        // 发送消息 - 全量返回
        virtual std::string sendMessage(const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam) override;
        // 发送消息 - 增量返回 - 流式响应
        virtual std::string sendMessageStream(const std::vector<Message>& messages, 
                                              const std::map<std::string, std::string>& requestParam,
                                              std::function<void(const std::string&, bool)> callback) override;   // callback: 对模型返回的增量数据如何处理，第一个参数为增量数据，第二个参数为是否为最后一个增量数据

    protected:
        bool _isAvailable = false;      // 标记模型是否有效
        std::string _apiKey;           // API密钥
        std::string _endpoint;         // 模型API endpoint  base url
    };
} // end ai_chat_sdk
