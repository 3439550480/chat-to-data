#include "../include/ai_chat_sdk/GeminiProvider.h"
#include "../include/ai_chat_sdk/util/myLog.h"

#include <httplib.h>
#include <jsoncpp/json/forwards.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>

namespace ai_chat_sdk{

     // 初始化模型
        bool GeminiProvider::initModel(const std::map<std::string, std::string>& modelConfig)
        {
            // 从modelConfig中获取API密钥和endpoint
            auto it = modelConfig.find("api_key");
            if (it == modelConfig.end())
            {
                ERR("GeminiProvider::initModel: api_Key not found in modelConfig");
                return false;
            }
            _apiKey = it->second;

            it = modelConfig.find("endpoint");
            if (it == modelConfig.end()){
                _endpoint = "https://generativelanguage.googleapis.com";
            }else{
                _endpoint = it->second;
            }
            

            _isAvailable = true;
            INFO("GeminiProvider::initModel: init model success, endpoint: {}", _endpoint);
            return true;
        }
        
        // 检测模型是否有效
        bool GeminiProvider::isAvailable() const
        {
            return _isAvailable;
        }
        // 获取模型名称
        std::string GeminiProvider::getModelName() const
        {
            return "gemini-2.0-flash";
        }

        // 获取模型描述
        std::string GeminiProvider::getModelDesc() const
        {
            return "Google 的急速响应模型，转为大模型部署和快速交互的场景设计";
        }

        // 发送消息 - 全量返回
        std::string GeminiProvider::sendMessage(const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam)
        {
            // 检测模型是否可用
            if (!isAvailable())
            {
                ERR("GeminiProvider::sendMessage: model not available");
                return "";
            }

            // 构造请求参数
            // 获取采用温度 和 max_tokens
            float temperature = 0.7f;
            int max_tokens = 2048;
            if (requestParam.find("temperature") != requestParam.end())
            {
                temperature = std::stof(requestParam.at("temperature"));
            }

            if (requestParam.find("max_tokens") != requestParam.end())
            {
                max_tokens = std::stoi(requestParam.at("max_tokens"));
            }

            // 构建历史消息
            Json::Value messsageArray(Json::arrayValue);
            for (const auto& message : messages)
            {
                Json::Value messageObj(Json::objectValue);
                messageObj["role"] = message._role;
                messageObj["content"] = message._content;
                messsageArray.append(messageObj);
            }

            // 构建请求体
            Json::Value requestBody(Json::objectValue);
            requestBody["model"] = getModelName();
            requestBody["messages"] = messsageArray;
            requestBody["temperature"] = temperature;
            requestBody["max_tokens"] = max_tokens;

            // 序列化请求体
            Json::StreamWriterBuilder writerBuilder;
            writerBuilder["indentation"] = "";
            std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

            // 创建HTTP客户端
            httplib::Client client(_endpoint);
            client.set_connection_timeout(30, 0);
            client.set_read_timeout(60, 0);
            client.set_proxy("127.0.0.1", 7890);

            // 设置请求头
            httplib::Headers headers = {
                {"Authorization", "Bearer " + _apiKey}
            };

            // 发送POST请求
            auto response = client.Post("/v1beta/openai/chat/completions", headers, requestBodyStr, "application/json");
            if (!response)
            {
                ERR("GeminiProvider::sendMessage: failed to send request, error: {}", to_string(response.error()));
                return "";
            }

            INFO("GeminiProvider::sendMessage: response status: {}", response->status);
            INFO("GeminiProvider::sendMessage: response body: {}", response->body);

            // 检测响应是否成功
            if (response->status != 200)
            {
                ERR("GeminiProvider::sendMessage: failed to send request, status: {}", response->status);
                return "";
            }

            // 解析响应体
            Json::CharReaderBuilder readerBuilder;
            Json::Value responseBody;
            std::stringstream responseStream(response->body);
            std::string jsonError;
            if (!Json::parseFromStream(readerBuilder, responseStream, &responseBody, &jsonError))
            {
                ERR("GeminiProvider::sendMessage: failed to parse response body, error: {}", jsonError);
                return "";
            }

            // 解析模型响应内容
            if(responseBody.isMember("choices") && responseBody["choices"].isArray() && !responseBody["choices"].empty())
            {
                Json::Value choice = responseBody["choices"][0];
                if(choice.isMember("message") && choice["message"].isMember("content"))
                {
                    std::string reply = choice["message"]["content"].asString();
                    INFO("GeminiProvider::sendMessage: reply: {}", reply);
                    return reply;
                }
            }

            // 解析失败
            ERR("Invalid response body from Gemini API");
            return "";
        }
        // 发送消息 - 增量返回 - 流式响应
        std::string GeminiProvider::sendMessageStream(const std::vector<Message>& messages, 
                                              const std::map<std::string, std::string>& requestParam,
                                              std::function<void(const std::string&, bool)> callback)
        {
            // 检测模型是否可用
            if (!isAvailable()){
                ERR("GeminiProvider::sendMessageStream: model not available");
                return "";
            }

            // 构造请求参数
            // 获取采用温度 和 max_tokens
            float temperature = 0.7f;
            int max_tokens = 2048;
            if (requestParam.find("temperature") != requestParam.end()){
                temperature = std::stof(requestParam.at("temperature"));
            }

            if (requestParam.find("max_tokens") != requestParam.end()){
                max_tokens = std::stoi(requestParam.at("max_tokens"));
            }

            // 构建历史消息
            Json::Value messsageArray(Json::arrayValue);
            for (const auto& message : messages){
                Json::Value messageObj(Json::objectValue);
                messageObj["role"] = message._role;
                messageObj["content"] = message._content;
                messsageArray.append(messageObj);
            }

            // 构造请求体
            Json::Value requestBody(Json::objectValue);
            requestBody["model"] = getModelName();
            requestBody["messages"] = messsageArray;
            requestBody["temperature"] = temperature;
            requestBody["max_tokens"] = max_tokens;
            requestBody["stream"] = true;

            // 序列化请求体
            Json::StreamWriterBuilder writerBuilder;
            writerBuilder["indentation"] = "";
            std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

            // 创建HTTP客户端
            httplib::Client client(_endpoint);
            client.set_connection_timeout(60, 0);
            client.set_read_timeout(300, 0);
            client.set_proxy("127.0.0.1", 7890);

            // 设置请求头
            httplib::Headers headers = {
                {"Authorization", "Bearer " + _apiKey}
            };

            // 流式处理相关变量
            std::string buffer;
            bool gotError = false;
            std::string errorMsg;
            int statusCode = 0;
            bool streamFinish = false;
            std::string fullData;

            // 创建请求对象
            httplib::Request request;
            request.method = "POST";
            request.path = "/v1beta/openai/chat/completions";
            request.body = requestBodyStr;
            request.headers = headers;
            // 处理响应头
            request.response_handler = [&](const httplib::Response& res) {
                statusCode = res.status;
                if (statusCode != 200){
                    gotError = true;
                    errorMsg = "HTTP status code: " + std::to_string(statusCode);
                    return false;
                }

                return true;
            };

            // 处理流式响应体
            request.content_receiver = [&](const char* data, size_t dataLength, size_t offset, size_t totalLength) {
                // 如果HTTP请求失败，不用继续接收后续数据
                if(gotError){
                    return false;
                }

                buffer.append(data, dataLength);

                // 处理所有增量数据，数据之间以\n\n间隔
                size_t pos = 0;
                while ((pos = buffer.find("\n\n")) != std::string::npos){
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 2);
                    if (line.empty() || line[0] == ':'){
                        continue;
                    }

                    if(line.compare(0, 6, "data: ") == 0){
                        std::string dataStr = line.substr(6);
                        
                        // 处理结尾标记
                        if(dataStr == "[DONE]"){
                            callback("", true);
                            streamFinish = true;
                            return true;
                        }

                        // jsonStr中为模型返回的数据的json格式序列化之后的结果--反序列化
                        Json::Value chunk;
                        Json::CharReaderBuilder reader;
                        std::stringstream ss(dataStr);
                        std::string errors;
                        if (Json::parseFromStream(reader, ss, &chunk, &errors)){
                            if(chunk.isMember("choices") && chunk["choices"].isArray() && !chunk["choices"].empty()){
                                Json::Value choice = chunk["choices"][0];
                                if(choice.isMember("delta") && choice["delta"].isObject() && choice["delta"].isMember("content")){
                                    std::string deltaContent = choice["delta"]["content"].asString();
                                    fullData += deltaContent;
                                    callback(deltaContent, false);
                                }
                            }
                        }else{
                            ERR("GeminiProvider::sendMessageStream: failed to parse json chunk, errors: {}", errors);
                            return false;
                        }
                    }
                }

                return true;
            };

            // 给我模型发送请求
            auto res = client.send(request);
            if(!res){
                ERR("GeminiProvider::sendMessageStream: failed to send request, error: {}", to_string(res.error()));
                return "";
            }

            // 确保流式操作正式结束
            if(!streamFinish){
                WARN("stream ended without [DONE] marker");
                callback("", true);
            }

            return fullData;
        }


} // end ai_chat_sdk
