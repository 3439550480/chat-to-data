#include "../include/ai_chat_sdk/DeepSeekProvider.h"
#include "../include/ai_chat_sdk/util/myLog.h"
#include <cstdint>
#include <jsoncpp/json/json.h>
#include <httplib.h>
#include <jsoncpp/json/reader.h>

namespace ai_chat_sdk{
    // DeepSeekProvider 类
    bool DeepSeekProvider::initModel(const std::map<std::string, std::string>& modelConfig){
        // 初始化API Key
        auto it = modelConfig.find("api_key");
        if(it == modelConfig.end()){
            ERR("DeepSeekProvider initModel api_key not found");
            return false;
        }else{
            _apiKey = it->second;
        }

        // 初始化Base URL
        it = modelConfig.find("endpoint");
        if(it == modelConfig.end()){
            _endpoint = "https://api.deepseek.com";
        }else{
            _endpoint = it->second;
        }
        
        _isAvailable = true;
        INFO("DeepSeekProvider initModel success, endpoint: {}",_endpoint);
        return true;
    }

    // 检测模型是否可用
    bool DeepSeekProvider::isAvailable() const{
        return _isAvailable;
    }

    // 获取模型名称
    std::string DeepSeekProvider::getModelName() const{
        return "deepseek-chat";
    }

    // 获取模型的描述信息
    std::string DeepSeekProvider::getModelDesc() const{
        return "一款实用性强、中文优化的通用对话助手，适合日常问答与创作";
    }

    // 
    std::string DeepSeekProvider::sendMessage(const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam)
    {
        // 1. 检测模型是否可用
        if(!isAvailable()){
            ERR("DeepSeekProvider sendMessage model not available");
            return "";
        }

        // 2. 构造请求参数
        double temperature = 0.7;
        int maxTokens = 2048;
        if(requestParam.find("temperature") != requestParam.end()){
            temperature = std::stod(requestParam.at("temperature"));
        }
        if(requestParam.find("max_tokens") != requestParam.end()){
            maxTokens = std::stoi(requestParam.at("max_tokens"));
        }

        // 构造历史消息
        Json::Value messageArray(Json::arrayValue);
        for(const auto& message : messages){
            Json::Value messageObject;
            messageObject["role"] = message._role;
            messageObject["content"] = message._content;
            messageArray.append(messageObject);
        }

        // 3. 构造请求体
        Json::Value requestBody;
        requestBody["model"] = getModelName();
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = maxTokens;

        // 4. 序列化
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);
        INFO("DeepSeekProvider sendMessage requestBody: {}", requestBodyStr);

        // 5. 使用cpp-httplib库构造HTTP客户端
        httplib::Client client(_endpoint.c_str());
        client.set_connection_timeout(30, 0);     // 连接超时时间为30秒
        client.set_read_timeout(60, 0);           // 设置超时时间为60秒

        // 设置请求头
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _apiKey},
            {"Content-Type", "application/json"}
        };

        // 6. 发送POST请求
        auto response = client.Post("/v1/chat/completions", headers, requestBodyStr, "application/json");
        if(!response){
            ERR("DeepSeekProvider sendMessage POST request failed");
            return "";
        }
        INFO("DeepSeekProvider sendMessage POST request success, status : {}", response->status);
        INFO("DeepSeekProvider sendMessage POST request success, body : {}", response->body);

        // 检测响应是否成功
        if(response->status != 200){
            return "";
        }

        // 7. 解析响应体
        Json::Value responseBody;
        Json::CharReaderBuilder readerBuilder;
        std::string parseError;
        std::istringstream responseStream(response->body);
        if(Json::parseFromStream(readerBuilder, responseStream, &responseBody, &parseError)){
            // 获取message数组
            if(responseBody.isMember("choices") && responseBody["choices"].isArray() && !responseBody["choices"].empty()){
                auto choice = responseBody["choices"][0];
                if(choice.isMember("message") && choice["message"].isMember("content")){
                    std::string replyContent = choice["message"]["content"].asString();
                    INFO("DeepSeekProvider response text: {}", replyContent);
                    return replyContent;
                }
            }
        }

        // 8. json解析失败
        ERR("DeepSeekProvider sendMessage POST response body parse failed, error");
        return "deepseek response json parse failed";
    }
    
        // 发送消息 - 增量返回 - 流式响应
    std::string DeepSeekProvider::sendMessageStream(const std::vector<Message>& messages, 
                                             const std::map<std::string, std::string>& requestParam,
                                             std::function<void(const std::string&, bool)> callback)
    {
        // 1. 检测模型是否可用
        if(!isAvailable()){
            ERR("DeepSeekProvider sendMessageStream model not available");
            return "";
        }

        // 2. 构造请求参数
        double temperature = 0.7;
        int maxTokens = 2048;
        if(requestParam.find("temperature") != requestParam.end()){
            temperature = std::stod(requestParam.at("temperature"));
        }
        if(requestParam.find("max_tokens") != requestParam.end()){
            maxTokens = std::stoi(requestParam.at("max_tokens"));
        }

        // 构造历史消息
        Json::Value messageArray(Json::arrayValue);
        for(const auto& message : messages){
            Json::Value messageObject;
            messageObject["role"] = message._role;
            messageObject["content"] = message._content;
            messageArray.append(messageObject);
        }

        // 3. 构造请求体
        Json::Value requestBody;
        requestBody["model"] = getModelName();
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = maxTokens;
        requestBody["stream"] = true;

        // 4. 序列化
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);
        INFO("DeepSeekProvider sendMessageStream requestBody: {}", requestBodyStr);

        // 5. 使用cpp-httplib库构造HTTP客户端
        httplib::Client client(_endpoint.c_str());
        client.set_connection_timeout(60, 0);     // 连接超时时间为30秒
        client.set_read_timeout(300, 0);          // 流式响应需要更长的时间，设置超时时间为300秒
        client.set_write_timeout(300, 0);         // 设置超时时间为60秒

        // 设置请求头
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _apiKey},
            {"Content-Type", "application/json"},
            {"Accept", "text/event-stream"}
        };

        // 流式处理变量
        std::string buffer;          // 接受流式响应的数据块
        bool gotError = false;       // 标记响应是否成功
        std::string errorMsg;        // 错误描述符
        int statusCode = 0;          // 响应状态码
        bool streamFinish = false;   // 标记流式响应是否完成
        std::string fullResponse;    // 累积完整的响应

        // 创建请求对象
        httplib::Request req;
        req.method = "POST";
        req.path = "/v1/chat/completions";
        req.headers = headers;
        req.body = requestBodyStr;
        // 设置响应处理器
        req.response_handler = [&](const httplib::Response& res) {
            if(res.status != 200){
                gotError = true;
                errorMsg = "HTTP status code: " + std::to_string(res.status);
                return false;    // 终止请求
            }
            return true;   // 继续接收后续数据
        };

        // 设置数据接收处理器--解析流式响应的每个块的数据
        req.content_receiver = [&](const char* data, size_t len, size_t offset, size_t totalLength){
            // 验证响应头是否错误，如果出错就不需要再继续接收数据
            if(gotError){
                return false;
            }

            // 追加数据到buffer
            buffer.append(data, len);
            INFO("DeepSeekProvider sendMessageStream buffer: {}", buffer);

            // 处理所有的流式响应的数据块，注意：数据块之间是一个\n\n分隔
            size_t pos= 0;
            while((pos = buffer.find("\n\n")) != std::string::npos){
                // 截取当前找到的数据块
                std::string chunk = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                // 解析该块响应数据的中模型返回的有效数据
                // 处理空行和注释，注意：以:开头的行是注释行，需要忽略
                if(chunk.empty() || chunk[0] == ':'){
                    continue;
                }

                // 获取模型返回的有效数据
                if(chunk.compare(0, 6, "data: ") == 0){
                    std::string modelData = chunk.substr(6);

                    // 检测是否为结束标记
                    if(modelData == "[DONE]"){
                        callback("", true);
                        streamFinish = true;
                        return true;
                    }

                    // 反序列化
                    Json::Value modelDataJson;
                    Json::CharReaderBuilder reader;
                    std::string errors;
                    std::istringstream modelDataStream(modelData);
                    if(Json::parseFromStream(reader, modelDataStream, &modelDataJson, &errors)){
                        // 模型返回的json格式的数据现在就保存在modelDataJson
                        if(modelDataJson.isMember("choices") &&
                          modelDataJson["choices"].isArray() && 
                          !modelDataJson["choices"].empty() &&
                          modelDataJson["choices"][0].isMember("delta") &&
                          modelDataJson["choices"][0]["delta"].isMember("content")){
                            std::string content = modelDataJson["choices"][0]["delta"]["content"].asString();
                            // 处理deltaContent，例如追加到fullResponse
                            fullResponse += content;

                            // 将本次解析出的模型返回的有效数据转给调用sendMessageStraem函数的用户使用---callback
                            callback(content, false);
                        }
                    }else{
                        WARN("DeepSeekProvider sendMessageStream parse modelDataJson error: {}", errors);
                    }
                }
            }
            return true;
        };

        // 给模型发送请求
        auto result = client.send(req);
        if(!result){
            // 请求发送失败，出现网络问题，比如DNS解析失败、连接超时
            ERR("Network error {}", to_string(result.error()));
            return "";
        }

        // 确保流式操作正确结束
        if(!streamFinish){
            WARN("stream ended without [DONE] marker");
            callback("", true);
        }

        return fullResponse;
    }




} // end ai_chat_sdk
