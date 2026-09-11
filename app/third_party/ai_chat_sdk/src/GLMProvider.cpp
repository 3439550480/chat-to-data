#include "../include/ai_chat_sdk/GLMProvider.h"
#include "../include/ai_chat_sdk/util/myLog.h"
#include <cstdint>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <httplib.h>

namespace ai_chat_sdk{
    // ===== 本项目补丁（PATCH 2）=====
    // 拆解 baseUrl 为「scheme://host[:port]」+「basePath」：
    // cpp-httplib 的 Client 只接收 scheme_host_port，路径前缀需自行拼到请求 path 上，
    // 因此这里把 https://open.bigmodel.cn/api/paas/v4 拆成
    //   client 用 https://open.bigmodel.cn ，请求 path 用 /api/paas/v4/chat/completions
    static std::string splitBaseUrl(const std::string& url, std::string& basePath){
        size_t pathPos = std::string::npos;
        size_t schemePos = url.find("://");
        if(schemePos != std::string::npos){
            pathPos = url.find('/', schemePos + 3);
        }else{
            pathPos = url.find('/');
        }
        if(pathPos == std::string::npos){
            basePath.clear();
            return url;
        }
        basePath = url.substr(pathPos);
        while(!basePath.empty() && basePath.back() == '/'){
            basePath.pop_back();
        }
        return url.substr(0, pathPos);
    }

    std::string GLMProvider::buildRequestPath(const std::string& basePath){
        // 兼容两种填写方式：只给到版本前缀（…/api/paas/v4）或已给完整接口路径
        if(basePath.size() >= 17 &&
           basePath.compare(basePath.size() - 17, 17, "/chat/completions") == 0){
            return basePath;
        }
        return basePath + "/chat/completions";
    }

    bool GLMProvider::initModel(const std::map<std::string, std::string>& modelConfig){
        // 1. api_key 必填
        auto it = modelConfig.find("api_key");
        if(it == modelConfig.end() || it->second.empty()){
            ERR("GLMProvider initModel api_key not found or empty");
            return false;
        }
        _apiKey = it->second;
        // 2. endpoint（base url）必填：GLM 为 OpenAI 兼容地址
        it = modelConfig.find("endpoint");
        if(it == modelConfig.end() || it->second.empty()){
            ERR("GLMProvider initModel endpoint not found or empty");
            return false;
        }
        _endpoint = it->second;
        // 3. 模型名必填（不再硬编码）
        it = modelConfig.find("model_name");
        if(it == modelConfig.end() || it->second.empty()){
            ERR("GLMProvider initModel model_name not found or empty");
            return false;
        }
        _modelName = it->second;
        // 4. 描述可选
        it = modelConfig.find("model_desc");
        _modelDesc = (it != modelConfig.end() && !it->second.empty())
                         ? it->second : "GLM 通用对话模型";

        _isAvailable = true;
        INFO("GLMProvider initModel success, model={}, endpoint={}", _modelName, _endpoint);
        return true;
    }

    bool GLMProvider::isAvailable() const{
        return _isAvailable;
    }

    std::string GLMProvider::getModelName() const{
        return _modelName;
    }

    std::string GLMProvider::getModelDesc() const{
        return _modelDesc;
    }

    // 发送消息 - 全量返回（结构照搬 DeepSeekProvider，模型名/endpoint 改为可配）
    std::string GLMProvider::sendMessage(const std::vector<Message>& messages,
                                         const std::map<std::string, std::string>& requestParam){
        if(!isAvailable()){
            ERR("GLMProvider sendMessage model not available");
            return "";
        }
        // 1. 请求参数
        double temperature = 0.7;
        int maxTokens = 2048;
        if(requestParam.find("temperature") != requestParam.end()){
            temperature = std::stod(requestParam.at("temperature"));
        }
        if(requestParam.find("max_tokens") != requestParam.end()){
            maxTokens = std::stoi(requestParam.at("max_tokens"));
        }
        // 2. 历史消息
        Json::Value messageArray(Json::arrayValue);
        for(const auto& message : messages){
            Json::Value messageObject;
            messageObject["role"] = message._role;
            messageObject["content"] = message._content;
            messageArray.append(messageObject);
        }
        // 3. 请求体
        Json::Value requestBody;
        requestBody["model"] = getModelName();
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = maxTokens;
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

        // 4. HTTP 请求（baseUrl 拆分：client 用 scheme://host，path 用 basePath + /chat/completions）
        std::string basePath;
        std::string clientUrl = splitBaseUrl(_endpoint, basePath);
        std::string requestPath = buildRequestPath(basePath);
        INFO("GLMProvider sendMessage url: {}{}", clientUrl, requestPath);

        httplib::Client client(clientUrl.c_str());
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(120, 0);
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _apiKey},
            {"Content-Type", "application/json"}
        };
        auto response = client.Post(requestPath.c_str(), headers, requestBodyStr, "application/json");
        if(!response){
            ERR("GLMProvider sendMessage POST request failed");
            return "";
        }
        if(response->status != 200){
            ERR("GLMProvider sendMessage POST failed, status={}, body={}",
                response->status, response->body);
            return "";
        }
        // 5. 解析响应
        Json::Value responseBody;
        Json::CharReaderBuilder readerBuilder;
        std::string parseError;
        std::istringstream responseStream(response->body);
        if(Json::parseFromStream(readerBuilder, responseStream, &responseBody, &parseError)){
            if(responseBody.isMember("choices") && responseBody["choices"].isArray() &&
               !responseBody["choices"].empty()){
                auto choice = responseBody["choices"][0];
                if(choice.isMember("message") && choice["message"].isMember("content")){
                    return choice["message"]["content"].asString();
                }
            }
        }
        ERR("GLMProvider sendMessage response parse failed: {}", parseError);
        return "";
    }

    // 发送消息 - 流式（SSE 解析逻辑同 DeepSeekProvider）
    std::string GLMProvider::sendMessageStream(const std::vector<Message>& messages,
                                               const std::map<std::string, std::string>& requestParam,
                                               std::function<void(const std::string&, bool)> callback){
        if(!isAvailable()){
            ERR("GLMProvider sendMessageStream model not available");
            return "";
        }
        // 1. 请求参数
        double temperature = 0.7;
        int maxTokens = 2048;
        if(requestParam.find("temperature") != requestParam.end()){
            temperature = std::stod(requestParam.at("temperature"));
        }
        if(requestParam.find("max_tokens") != requestParam.end()){
            maxTokens = std::stoi(requestParam.at("max_tokens"));
        }
        // 2. 历史消息
        Json::Value messageArray(Json::arrayValue);
        for(const auto& message : messages){
            Json::Value messageObject;
            messageObject["role"] = message._role;
            messageObject["content"] = message._content;
            messageArray.append(messageObject);
        }
        // 3. 请求体（stream=true）
        Json::Value requestBody;
        requestBody["model"] = getModelName();
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = maxTokens;
        requestBody["stream"] = true;
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

        // 4. HTTP 客户端
        std::string basePath;
        std::string clientUrl = splitBaseUrl(_endpoint, basePath);
        std::string requestPath = buildRequestPath(basePath);
        INFO("GLMProvider sendMessageStream url: {}{}, model={}", clientUrl, requestPath, _modelName);

        httplib::Client client(clientUrl.c_str());
        client.set_connection_timeout(60, 0);
        client.set_read_timeout(300, 0);
        client.set_write_timeout(300, 0);
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _apiKey},
            {"Content-Type", "application/json"},
            {"Accept", "text/event-stream"}
        };

        std::string buffer;
        bool gotError = false;
        std::string errorMsg;
        bool streamFinish = false;
        std::string fullResponse;

        httplib::Request req;
        req.method = "POST";
        req.path = requestPath;
        req.headers = headers;
        req.body = requestBodyStr;
        req.response_handler = [&](const httplib::Response& res){
            if(res.status != 200){
                gotError = true;
                errorMsg = "HTTP status code: " + std::to_string(res.status) + ", body: " + res.body;
                return false;
            }
            return true;
        };
        req.content_receiver = [&](const char* data, size_t len, size_t /*offset*/, size_t /*totalLength*/){
            if(gotError){
                return false;
            }
            buffer.append(data, len);
            size_t pos = 0;
            // SSE 事件块以 \n\n 分隔
            while((pos = buffer.find("\n\n")) != std::string::npos){
                std::string chunk = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);
                if(chunk.empty() || chunk[0] == ':'){
                    continue;   // 空行与注释行
                }
                if(chunk.compare(0, 6, "data: ") == 0){
                    std::string modelData = chunk.substr(6);
                    if(modelData == "[DONE]"){
                        callback("", true);
                        streamFinish = true;
                        return true;
                    }
                    Json::Value modelDataJson;
                    Json::CharReaderBuilder reader;
                    std::string errors;
                    std::istringstream modelDataStream(modelData);
                    if(Json::parseFromStream(reader, modelDataStream, &modelDataJson, &errors)){
                        if(modelDataJson.isMember("choices") &&
                           modelDataJson["choices"].isArray() &&
                           !modelDataJson["choices"].empty() &&
                           modelDataJson["choices"][0].isMember("delta") &&
                           modelDataJson["choices"][0]["delta"].isMember("content")){
                            std::string content = modelDataJson["choices"][0]["delta"]["content"].asString();
                            fullResponse += content;
                            callback(content, false);
                        }
                    }else{
                        WARN("GLMProvider sendMessageStream parse delta error: {}", errors);
                    }
                }
            }
            return true;
        };

        auto result = client.send(req);
        if(!result){
            ERR("GLMProvider sendMessageStream network error: {}", to_string(result.error()));
            if(!errorMsg.empty()){
                ERR("GLMProvider sendMessageStream detail: {}", errorMsg);
            }
            return "";
        }
        if(!streamFinish){
            WARN("GLMProvider stream ended without [DONE] marker");
            callback("", true);
        }
        return fullResponse;
    }
} // end ai_chat_sdk
