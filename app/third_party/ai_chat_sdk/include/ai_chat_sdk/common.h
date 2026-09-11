#pragma once
#include <string>
#include <ctime>
#include <vector>

namespace ai_chat_sdk{

// 消息结构
struct Message{
    std::string _messageId;       // 消息ID
    std::string _role;            // 角色，如user、assistant等
    std::string _content;         // 消息内容
    std::time_t _timestamp;       // 消息发送时间戳

    // 构造函数
    Message(const std::string& role = "", const std::string& content = "")
        : _role(role), _content(content), _timestamp(0)
    {}
};


// 模型的公共配置信息
struct Config{
    std::string _modelName;       // 模型名称
    double _temperature = 0.7;    // 温度参数，用于控制生成文本的随机性
    int _maxTokens = 2048;       // 最大生成令牌数

    virtual ~Config() = default;  // 添加虚函数的目的主要是为了实现：向下转型时的安全性
};

// 通过API方式接入云端模型
struct APIConfig : public Config{
    std::string _apiKey;          // API密钥
    // ===== 本项目补丁（PATCH 1）=====
    // 原实现只支持 api_key（endpoint 与模型名在各 Provider 内硬编码，仅覆盖
    // deepseek-chat / gpt-4o-mini / gemini-2.0-flash），无法接入 GLM 等
    // 需自定义 baseUrl/模型名的 OpenAI 兼容服务。新增以下两个可选字段：
    //   _baseUrl 非空 ⇒ 按「可配置 OpenAI 兼容 Provider」(GLMProvider) 接入
    //   _modelDesc 仅用于模型列表展示，可留空
    std::string _baseUrl;         // 模型API base url（如 https://open.bigmodel.cn/api/paas/v4）
    std::string _modelDesc;       // 模型描述（展示用，可空）
};

// 通过Ollama接入本地模型---不需要apikey
struct OllamaConfig : public Config{
    std::string _modelName;       // 模型名称
    std::string _modelDesc;       // 模型描述
    std::string _endpoint;        // 模型API endpoint  base url
};



// LLM信息
struct ModelInfo{
    std::string _modelName;       // 模型名称
    std::string _modelDesc;       // 模型描述
    std::string _provider;        // 模型提供者
    std::string _endpoint;        // 模型API endpoint  base url
    bool _isAvailable = false;    // 模型是否可用

    ModelInfo(const std::string& modelName = "", const std::string& modelDesc = "", const std::string& provider = "", const std::string& endpoint = "")
        : _modelName(modelName), _modelDesc(modelDesc), _provider(provider), _endpoint(endpoint)
    {}
};

// 会话信息
struct Session{
    std::string _sessionId;       // 会话ID
    std::string _modelName;       // 会话使用的模型名称
    std::vector<Message> _messages; // 会话中的消息列表
    std::time_t _createdAt;        // 会话创建时间戳
    std::time_t _updatedAt;        // 会话最后更新时间戳

    // 构造函数
    Session(const std::string& modelName = "")
        : _modelName(modelName)
    {}
};

} // end ai_chat_sdk