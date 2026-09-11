#include "../include/ai_chat_sdk/ChatSDK.h"
#include "../include/ai_chat_sdk/DeepSeekProvider.h"
#include "../include/ai_chat_sdk/ChatGPTProvider.h"
#include "../include/ai_chat_sdk/GeminiProvider.h"
#include "../include/ai_chat_sdk/OllamaLLMProvider.h"
#include "../include/ai_chat_sdk/GLMProvider.h"     // 本项目补丁（PATCH 3）
#include "../include/ai_chat_sdk/util/myLog.h"
#include <memory>
#include <unordered_set>



namespace ai_chat_sdk {

// 初始化支持的模型
bool ChatSDK::initModels(const std::vector<std::shared_ptr<Config>>& configs){
    // 注册所支持的模型
    registerAllProvider(configs);

    // 初始化所支持的模型
    initProviders(configs);

    _initialized = true;
    return true;
}


// 注册所有支持的模型
void ChatSDK::registerAllProvider(const std::vector<std::shared_ptr<Config>>& configs){
    // deepseek-chat
    if(!_llmManager.isModelAvailable("deepseek-chat")){
        auto deepseekProvider = std::make_unique<DeepSeekProvider>();

        // unuqe_ptr不允许自动类型转换，即使类型之间存在继承关系
        _llmManager.registerProvider("deepseek-chat", std::move(deepseekProvider));
        INFO("deepseek-chat provider registered successed");
    }

    // gpt-4o-mini
    if(!_llmManager.isModelAvailable("gpt-4o-mini")){
        auto gpt4oProvider = std::make_unique<ChatGPTProvider>();

        // unuqe_ptr不允许自动类型转换，即使类型之间存在继承关系
        _llmManager.registerProvider("gpt-4o-mini", std::move(gpt4oProvider));
        INFO("gpt-4o-mini provider registered successed");
    }

    // gemini-2.0-flash
    if(!_llmManager.isModelAvailable("gemini-2.0-flash")){
        auto geminiProvider = std::make_unique<GeminiProvider>();

        // unuqe_ptr不允许自动类型转换，即使类型之间存在继承关系
        _llmManager.registerProvider("gemini-2.0-flash", std::move(geminiProvider));
        INFO("gemini-2.0-flash provider registered successed");
    }

    // Ollama接入本地模型---模型信息通过用户传递 configs
    std::unordered_set<std::string> modelNames;
    for(const auto& config : configs){
        // config是不是OllamaConfig的一个对象呢
        auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config);
        if(ollamaConfig){
            auto modelName = ollamaConfig->_modelName;
            if(modelNames.find(modelName) == modelNames.end()){
                // 该模型还没有注册
                modelNames.insert(modelName);

                if(!_llmManager.isModelAvailable(modelName)){
                    // 该模型还没有注册
                    _llmManager.registerProvider(modelName, std::make_unique<OllamaLLMProvider>());
                    INFO("OllamaLLMProvider {} registered successed", modelName);
                }
            }
        }
    }

    // ===== 本项目补丁（PATCH 3）=====
    // OpenAI 兼容服务（GLM 等）：APIConfig 带非空 _baseUrl 时注册 GLMProvider，
    // 模型名由配置决定（原三个硬编码模型与 Ollama 路径完全不受影响）
    for(const auto& config : configs){
        auto apiConfig = std::dynamic_pointer_cast<APIConfig>(config);
        if(apiConfig && !apiConfig->_baseUrl.empty() && !apiConfig->_modelName.empty()){
            const auto& modelName = apiConfig->_modelName;
            if(!_llmManager.isModelAvailable(modelName)){
                _llmManager.registerProvider(modelName, std::make_unique<GLMProvider>());
                INFO("GLMProvider {} registered successed (baseUrl={})",
                     modelName, apiConfig->_baseUrl);
            }
        }
    }
}

// 初始化所有支持的模型
void ChatSDK::initProviders(const std::vector<std::shared_ptr<Config>>& configs){
    for(const auto& config : configs){
        if(auto apiConfig = std::dynamic_pointer_cast<APIConfig>(config)){
            // ===== 本项目补丁（PATCH 3）：带 _baseUrl 的 API 配置走 OpenAI 兼容 Provider =====
            if(!apiConfig->_baseUrl.empty()){
                initAPIModelProviders(apiConfig->_modelName, apiConfig);
            }else if(apiConfig->_modelName == "deepseek-chat" || 
               apiConfig->_modelName == "gpt-4o-mini" ||
               apiConfig->_modelName == "gemini-2.0-flash"){
                 // 支持的云端模型
                 initAPIModelProviders(apiConfig->_modelName, apiConfig);
            }else{
                ERR("Model {} is not supported", apiConfig->_modelName);
            }
        }else if(auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config)){
            // 初始化Ollama模型提供者
            initOllamaModelProviders(ollamaConfig->_modelName, ollamaConfig);
        }else{
            ERR("Config {} is not supported", config->_modelName);
        }
    }
}

// 初始化模型提供者 - API模型提供者
bool ChatSDK::initAPIModelProviders(const std::string& modelName, const std::shared_ptr<APIConfig>& apiConfig){
    // 参数检测
    if(modelName.empty()){
        ERR("ChatSDK::initAPIModelProviders: modelName is empty");
        return false;
    }

    if(!apiConfig || apiConfig->_apiKey.empty()){
        ERR("ChatSDK::initAPIModelProviders: apiKey is empty");
        return false;
    }

    // 初始化模型提供者
    if(_llmManager.isModelAvailable(modelName)){
        INFO("ChatSDK::initAPIModelProviders: model {} is already available", modelName);
        return true;
    }

    // 初始化模型
    std::map<std::string, std::string> modelParams;
    modelParams["api_key"] = apiConfig->_apiKey;
    // ===== 本项目补丁（PATCH 3）=====
    // OpenAI 兼容服务（GLM）需要 baseUrl / 模型名 / 描述 一并下传；
    // 非 baseUrl 路径（deepseek/gpt/gemini）保持原样，仅传 api_key
    if(!apiConfig->_baseUrl.empty()){
        modelParams["endpoint"] = apiConfig->_baseUrl;
        modelParams["model_name"] = apiConfig->_modelName;
        modelParams["model_desc"] = apiConfig->_modelDesc;
    }
    if(!_llmManager.initModel(modelName, modelParams)){
        ERR("ChatSDK::initAPIModelProviders: init model {} failed", modelName);
        return false;
    }

    // 模型配置
    _modelConfigs[modelName] = apiConfig;
    INFO("ChatSDK::initAPIModelProviders: model {} init successed", modelName);
    return true;
}

// 初始化模型提供者 - Ollama模型提供者
bool ChatSDK::initOllamaModelProviders(const std::string& modelName, const std::shared_ptr<OllamaConfig>& ollamaConfig){
    // 参数检测
    if(modelName.empty()){
        ERR("ChatSDK::initOllamaModelProviders: modelName is empty");
        return false;
    }
    if(!ollamaConfig || ollamaConfig->_endpoint.empty()){
        ERR("ChatSDK::initOllamaModelProviders: endpoint is empty");
        return false;
    }

    // 初始化模型提供者
    if(_llmManager.isModelAvailable(modelName)){
        INFO("ChatSDK::initOllamaModelProviders: model {} is already available", modelName);
        return true;
    }

    // 初始化模型
    std::map<std::string, std::string> modelParams;
    modelParams["model_name"] = modelName;
    modelParams["model_desc"] = ollamaConfig->_modelDesc;
    modelParams["endpoint"] = ollamaConfig->_endpoint;
    if(!_llmManager.initModel(modelName, modelParams)){
        ERR("ChatSDK::initOllamaModelProviders: init model {} failed", modelName);
        return false;
    }

    // 模型配置
    _modelConfigs[modelName] = ollamaConfig;
    INFO("ChatSDK::initOllamaModelProviders: model {} init successed", modelName);
    return true;
}

// 创建会话 - 云端模型
std::string ChatSDK::createSession(const std::string& modelName){
    // 检测SDK是否初始化成功
    if(!_initialized){
        ERR("ChatSDK::createSession: SDK is not initialized");
        return "";
    }

    // 通过sessionManager来创建会话
    auto sessionId = _sessionManager.createSession(modelName);
    if(sessionId.empty()){
        ERR("ChatSDK::createSession: create session failed");
        return "";
    }
    INFO("ChatSDK::createSession: create session {} successed", sessionId);
    return sessionId;
}

// 获取指定会话
std::shared_ptr<Session> ChatSDK::getSession(const std::string& sessionId){
    // 检测SDK是否初始化成功
    if(!_initialized){
        ERR("ChatSDK::getSession: SDK is not initialized");
        return nullptr;
    }

    // 通过sessionManager来获取会话
    auto session = _sessionManager.getSession(sessionId);
    if(!session){
        ERR("ChatSDK::getSession: session {} not found", sessionId);
        return nullptr;
    }
    return session;
}

// 获取所有会话列表
std::vector<std::string> ChatSDK::getSessionLists()const{
    // 检测SDK是否初始化成功
    if(!_initialized){
        ERR("ChatSDK::getSessionLists: SDK is not initialized");
        return {};
    }

    // 通过sessionManager来获取所有会话
    return _sessionManager.getSessionLists();
}

// 删除会话
bool ChatSDK::deleteSession(const std::string& sessionId){
    // 检测SDK是否初始化成功
    if(!_initialized){
        ERR("ChatSDK::deleteSession: SDK is not initialized");
        return false;
    }

    // 通过sessionManager来删除会话
    bool result = _sessionManager.deleteSession(sessionId);
    if(!result){
        ERR("ChatSDK::deleteSession: delete session {} failed", sessionId);
        return false;
    }
    INFO("ChatSDK::deleteSession: delete session {} successed", sessionId);
    return true;
}

// 获取可用模型列表
std::vector<ModelInfo> ChatSDK::getAvailableModels()const
{
    return _llmManager.getAvailableModels();
}


// 给模型发消息 - 全量返回
std::string ChatSDK::sendMessage(const std::string& sessionId, const std::string& message){
    // 检测SDK是否初始化成功
    if(!_initialized){
        ERR("ChatSDK::sendMessage: SDK is not initialized");
        return "";
    }

    // 获取sessionId对应的session对象
    auto session = _sessionManager.getSession(sessionId);
    if(!session){
        ERR("ChatSDK::sendMessage: session {} not found", sessionId);
        return "";
    }

    // 构造历史消息
    Message userMessage("user", message);
    _sessionManager.addMessage(sessionId, userMessage);
    auto historyMessages = _sessionManager.getHistroyMessages(sessionId);

    // 构建请求参数
    auto it = _modelConfigs.find(session->_modelName);
    if(it == _modelConfigs.end()){
        ERR("ChatSDK::sendMessage: model {} not found", session->_modelName);
        return "";
    }
    std::map<std::string, std::string> requestParam;
    requestParam["temperature"] = std::to_string(it->second->_temperature);
    requestParam["max_tokens"] = std::to_string(it->second->_maxTokens);

    // 调用LLMManager发送消息
    auto response = _llmManager.sendMessage(session->_modelName, historyMessages, requestParam);
    if(response.empty()){
        ERR("ChatSDK::sendMessage: send message to model {} failed", session->_modelName);
        return "";
    }

    // 添加助手消息并更新会话时间
    Message assistantMessage("assistant", response);
    _sessionManager.addMessage(sessionId, assistantMessage);
    _sessionManager.updateSessionTimestamp(sessionId);
    INFO("ChatSDK::sendMessage: send message to model {} successed", session->_modelName);
    return response;
}

// 给模型发送消息 - 增量返回
std::string ChatSDK::sendMessageStream(const std::string& sessionId, const std::string& message, std::function<void(const std::string&, bool)> callback){
    // 检测SDK是否初始化成功
    if(!_initialized){
        ERR("ChatSDK::sendMessageStream: SDK is not initialized");
        return "";
    }

    // 获取sessionId对应的session对象
    auto session = _sessionManager.getSession(sessionId);
    if(!session){
        ERR("ChatSDK::sendMessageStream: session {} not found", sessionId);
        return "";
    }

    // 构造历史消息
    Message userMessage("user", message);
    _sessionManager.addMessage(sessionId, userMessage);
    auto historyMessages = _sessionManager.getHistroyMessages(sessionId);

    // 构建请求参数
    auto it = _modelConfigs.find(session->_modelName);
    if(it == _modelConfigs.end()){
        ERR("ChatSDK::sendMessageStream: model {} not found", session->_modelName);
        return "";
    }
    std::map<std::string, std::string> requestParam;
    requestParam["temperature"] = std::to_string(it->second->_temperature);
    requestParam["max_tokens"] = std::to_string(it->second->_maxTokens);

    // 调用LLMManager发送消息
    auto response = _llmManager.sendMessageStream(session->_modelName, historyMessages, requestParam, callback);
    if(response.empty()){
        ERR("ChatSDK::sendMessageStream: send message to model {} failed", session->_modelName);
        return "";
    }

    // 添加助手消息并更新会话时间
    Message assistantMessage("assistant", response);
    _sessionManager.addMessage(sessionId, assistantMessage);
    _sessionManager.updateSessionTimestamp(sessionId);
    INFO("ChatSDK::sendMessageStream: send message to model {} successed", session->_modelName);
    return response;
}


} // end ai_chat_sdk
