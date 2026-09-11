#pragma once
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include "LLMManager.h"
#include "SessionManager.h"
#include "common.h"


namespace ai_chat_sdk{
// ChatSDK 类
    class ChatSDK{
    public:
        // 初始化模型
        bool initModels(const std::vector<std::shared_ptr<Config>>& configs);
        // 创建会话
        std::string createSession(const std::string& modelName);
        // 获取指定会话
        std::shared_ptr<Session> getSession(const std::string& sessionId);
        // 获取所有会话列表
        std::vector<std::string> getSessionLists() const;
        // 删除指定会话
        bool deleteSession(const std::string& sessionId);
        // 获取可用的模型信息
        std::vector<ModelInfo> getAvailableModels() const;

        // 发送消息 - 全量返回
        std::string sendMessage(const std::string& sessionId, const std::string& message);
        // 发送消息 - 增量返回 - 流式响应
        std::string sendMessageStream(const std::string& sessionId, const std::string& message, 
                                            std::function<void(const std::string&, bool)> callback);   // callback: 对模型返回的增量数据如何处理，第一个参数为增量数据，第二个参数为是否为最后一个增量数据

    private:
        // 注册所支持的模型
        void registerAllProvider(const std::vector<std::shared_ptr<Config>>& configs);
        // 初始化所支持的模型
        void initProviders(const std::vector<std::shared_ptr<Config>>& configs);
        // 初始化模型提供者 - API模型提供者
        bool initAPIModelProviders(const std::string& modelName, const std::shared_ptr<APIConfig>& apiConfig);
        // 初始化模型提供者 - Ollama模型提供者
        bool initOllamaModelProviders(const std::string& modelName, const std::shared_ptr<OllamaConfig>& config);

    private:
        bool _initialized = false;        // 是否初始化
        // key: 模型名称  value：模型配置信息
        std::unordered_map<std::string, std::shared_ptr<Config>> _modelConfigs;  // 模型配置
        LLMManager _llmManager;           // 主要和模型进行交互
    public:
        SessionManager _sessionManager;   // 主要和会话进行交互
    };
} // end ai_chat_sdk