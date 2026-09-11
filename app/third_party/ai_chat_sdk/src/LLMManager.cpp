#include "../include/ai_chat_sdk/LLMManager.h"
#include "../include/ai_chat_sdk/util/myLog.h"
#include "../include/ai_chat_sdk/common.h"

namespace ai_chat_sdk {

    // 注册LLM提供者
    bool LLMManager::registerProvider(const std::string& modelName, std::unique_ptr<LLMProvider> provider){
        // 参数检测
        if(!provider){
            ERR("cannot register nullptr provider, modelName = {}", modelName);
            return false;
        }

        // 注意：unique_ptr是防拷贝的，此处只能通过move的方式将资源转移给当前对象
        _providers[modelName] = std::move(provider);

        // 添加模型信息
        _modelInfos[modelName] = ModelInfo(modelName);

        // 模型初始化成功
        INFO("register provider success, modelName = {}", modelName);
        return true;
    }

    // 初始化指定模型
    bool LLMManager::initModel(const std::string& modelName, const std::map<std::string, std::string>& modelParam){
        // 检测模型是否注册
        auto it  = _providers.find(modelName);
        if(it == _providers.end()){
            ERR("model provider not found, modelName = {}", modelName);
            return false;
        }

        // 模型已经注册成功，可以初始化该模型
        bool isSuccess = it->second->initModel(modelParam);
        if(!isSuccess){
            ERR("init model failed, modelName = {}", modelName);
        }else{
            INFO("init model success, modelName = {}", modelName);
            _modelInfos[modelName]._modelDesc = it->second->getModelDesc();
            _modelInfos[modelName]._isAvailable = true;
        }

        return isSuccess;
    }

    // 获取可用模型
    std::vector<ModelInfo> LLMManager::getAvailableModels()const{
        std::vector<ModelInfo> models;
        for(const auto& pair : _modelInfos){
            if(pair.second._isAvailable){
                models.push_back(pair.second);
            }
        }
        return models;
    }

    // 检查模型是否可用
    bool LLMManager::isModelAvailable(const std::string& modelName)const{
        auto it = _modelInfos.find(modelName);
        if(it == _modelInfos.end()){
            return false;
        }

        return it->second._isAvailable;
    }


    // 发送消息给指定模型
    std::string LLMManager::sendMessage(const std::string& modelName, const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam){
        // 检测模型是否注册
        auto it = _providers.find(modelName);
        if(it == _providers.end()){
            ERR("model provider not found, modelName = {}", modelName);
            return "";
        }

        // 检测模型是否可用
        if(!it->second->isAvailable()){
            ERR("model not available, modelName = {}", modelName);
            return "";
        }

        // 模型已经注册，并且已经初始化成功
        return it->second->sendMessage(messages, requestParam);
    }

    // 发送消息流给指定模型
    std::string LLMManager::sendMessageStream(const std::string& modelName, const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam, std::function<void(const std::string&, bool)>& callback){
         // 检测模型是否注册
        auto it = _providers.find(modelName);
        if(it == _providers.end()){
            ERR("model provider not found, modelName = {}", modelName);
            return "";
        }

        // 检测模型是否可用
        if(!it->second->isAvailable()){
            ERR("model not available, modelName = {}", modelName);
            return "";
        }
        
        // 模型已经注册，并且已经初始化成功
        return it->second->sendMessageStream(messages, requestParam, callback);
    }

} // end ai_chat_sdk
