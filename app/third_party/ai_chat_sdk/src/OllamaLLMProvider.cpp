#include "../include/ai_chat_sdk/OllamaLLMProvider.h"
#include "../include/ai_chat_sdk/util/myLog.h"
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <httplib.h>
#include <sstream>

namespace ai_chat_sdk{

    // 初始化模型
    bool OllamaLLMProvider::initModel(const std::map<std::string, std::string>& modelConfig){
        // 初始化模型名称
        if(modelConfig.find("model_name") == modelConfig.end()){
            ERR("OllamaLLMProvider::initModel: model_name is not found in modelConfig");
            return false;
        }
        _modelName = modelConfig.at("model_name");

        // 初始化模型描述
        if(modelConfig.find("model_desc") == modelConfig.end()){
            ERR("OllamaLLMProvider::initModel: model_desc is not found in modelConfig");
            return false;
        }
        _modelDesc = modelConfig.at("model_desc");

        // 初始化endpoint
        if(modelConfig.find("endpoint") == modelConfig.end()){
            ERR("OllamaLLMProvider::initModel: endpoint is not found in modelConfig");
            return false;
        }
        _endpoint = modelConfig.at("endpoint");

        // 初始化模型是否有效
        _isAvailable = true;
        return true;
    }

    // 检测模型是否有效
    bool OllamaLLMProvider::isAvailable() const{
        return _isAvailable;
    }

    // 获取模型名称
    std::string OllamaLLMProvider::getModelName() const{
        return _modelName;
    }

    // 获取模型描述
    std::string OllamaLLMProvider::getModelDesc() const{
        return _modelDesc;
    }

    // 发送消息 - 全量返回
    std::string OllamaLLMProvider::sendMessage(const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam){
        // 检查模型是否有效
        if(!isAvailable()){
            ERR("OllamaLLMProvider::sendMessage: model is not available");
            return "";
        }

        // 构造请求参数
        // 构造温度值和最大tokens数
        float temperature = 0.7f;
        int maxTokens = 1024;
        if(requestParam.find("temperature") != requestParam.end()){
            temperature = std::stof(requestParam.at("temperature"));
        }
        if(requestParam.find("max_tokens") != requestParam.end()){
            maxTokens = std::stoi(requestParam.at("max_tokens"));
        }

        // 构建历史消息
        Json::Value messageArray(Json::arrayValue);
        for(const auto& message : messages){
            Json::Value messageObject(Json::objectValue);
            messageObject["role"] = message._role;
            messageObject["content"] = message._content;
            messageArray.append(messageObject);
        }

        // 构建请求体
        Json::Value options(Json::objectValue);
        options["temperature"] = temperature;
        options["num_ctx"] = maxTokens;
        
        Json::Value requestBody(Json::objectValue);
        requestBody["model"] = _modelName;
        requestBody["messages"] = messageArray;
        requestBody["options"] = options;
        requestBody["stream"] = false;

        // 序列化请求体
        Json::StreamWriterBuilder writerBuilder;
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

        // 创建http客户端
        httplib::Client client(_endpoint.c_str());
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        // 设置请求头
        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };
        // 发送POST请求
        auto response = client.Post("/api/chat", headers, requestBodyStr, "application/json");
        if(!response){
            ERR("OllamaLLMProvider::sendMessage: failed to send request, error: {}", to_string(response.error()));
            return "";
        }

        INFO("OllamaLLMProvider::sendMessage: response status: {}", response->status);
        INFO("OllamaLLMProvider::sendMessage: response body: {}", response->body);
        if(response->status != 200){
            ERR("OllamaLLMProvider::sendMessage: failed to send request, status: {}", response->status);
            return "";
        }

        // 反序列化
        Json::Value responseBody;
        Json::CharReaderBuilder reader;
        std::string errors;
        std::istringstream responseStream(response->body);
        if(!Json::parseFromStream(reader, responseStream, &responseBody, &errors)){
            ERR("OllamaLLMProvider::sendMessage: failed to parse response body, errors: {}", errors);
            return "";
        }

        // 提取模型返回的消息
        std::string modelResponse = "";
        if(responseBody.isMember("message") && responseBody["message"].isObject() && responseBody["message"].isMember("content")){
            modelResponse = responseBody["message"]["content"].asString();
            INFO("OllamaLLMProvider::sendMessage: modelResponse: {}", modelResponse);
            return modelResponse;
        }

        // 处理其他情况
        ERR("OllamaLLMProvider::sendMessage: invalid response format");
        return "";
    }

    // 发送消息 - 增量返回 - 流式响应
    std::string OllamaLLMProvider::sendMessageStream(const std::vector<Message>& messages, 
                                                      const std::map<std::string, std::string>& requestParam,
                                                      std::function<void(const std::string&, bool)> callback){
        // 检测模型是否可用
        if(!isAvailable()){
            ERR("OllamaLLMProvider::sendMessageStream: model is not available");
            return "";
        }

        // 构造请求参数
        // 构造温度值和最大tokens数
        float temperature = 0.7f;
        int maxTokens = 1024;
        if(requestParam.find("temperature") != requestParam.end()){
            temperature = std::stof(requestParam.at("temperature"));
        }
        if(requestParam.find("max_tokens") != requestParam.end()){
            maxTokens = std::stoi(requestParam.at("max_tokens"));
        }

        // 构建历史消息
        Json::Value messageArray(Json::arrayValue);
        for(const auto& message : messages){
            Json::Value messageObject(Json::objectValue);
            messageObject["role"] = message._role;
            messageObject["content"] = message._content;
            messageArray.append(messageObject);
        }

        // 构建请求体
        Json::Value options(Json::objectValue);
        options["temperature"] = temperature;
        options["num_ctx"] = maxTokens;
        
        Json::Value requestBody(Json::objectValue);
        requestBody["model"] = _modelName;
        requestBody["messages"] = messageArray;
        requestBody["options"] = options;
        requestBody["stream"] = true;

        // 序列化请求体
        Json::StreamWriterBuilder writerBuilder;
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

        // 创建http客户端
        httplib::Client client(_endpoint.c_str());
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(300, 0);
        // 设置请求头
        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };

        // 流式处理变量
        std::string buffer;
        bool gotError = false;
        std::string errorMsg;
        int statusCode = 0;
        bool streamFinish = false;
        std::string fullData;

        // 创建请求对象
        httplib::Request request;
        request.method = "POST";
        request.path = "/api/chat";
        request.headers = headers;
        request.body = requestBodyStr;
        // 响应头处理器
        request.response_handler = [&](const httplib::Response& res){
            statusCode = res.status;
            if(statusCode != 200){
                gotError = true;
                errorMsg = "OllamaLLMProvider::sendMessageStream: failed to send request, status: " + std::to_string(statusCode);
                return false;  // 终止请求
            }

            return true;
        };
        // 内容接收器
        request.content_receiver = [&](const char* data, size_t dataLen, size_t offset, size_t totalLength){
            // 如果http响应头出错，就不需要接收后续数据
            if(gotError){
                return false;  // 终止接收
            }

            buffer.append(data, dataLen);

            // 处理每个数据块，数据块之间是以\n间隔的
            // 注意：此处接收到的数据块并不是模型返回的SSE格式的数据，而是经过Ollama服务器处理之后的数据
            size_t pos = 0;
            while((pos = buffer.find("\n", pos)) != std::string::npos){
                std::string chunk = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                if(chunk.empty()){
                    continue;
                }

                // 反序列化
                Json::Value chunkJson;
                Json::CharReaderBuilder readerBuilder;
                std::string errors;
                std::istringstream chunkStream(chunk);
                if(!Json::parseFromStream(readerBuilder, chunkStream, &chunkJson, &errors)){
                    ERR("OllamaLLMProvider::sendMessageStream: failed to parse chunk json, errors: {}", errors);
                    continue;
                }

                // 处理结束标记
                if(chunkJson.get("done", false).asBool()){
                    streamFinish = true;
                    callback("", true);
                    return true;
                }

                // 提取增量数据
                if(chunkJson.isMember("message") && chunkJson["message"].isMember("content")){
                    std::string delta = chunkJson["message"]["content"].asString();
                    fullData += delta;
                    callback(delta, false);
                }
            }
            return true;
        };

        // 给Ollama服务器发送请求
        auto response = client.send(request);
        if(!response){
            ERR("OllamaLLMProvider::sendMessageStream: failed to send request, error: {}", to_string(response.error()));
            return "";
        }

        // 确保流式响应正常结束
        if(!streamFinish){
            ERR("OllamaLLMProvider::sendMessageStream: stream not finish, fullData: {}", fullData);
            callback("", true);
        }

        return fullData;
    }


} // end ai_chat_sdk
