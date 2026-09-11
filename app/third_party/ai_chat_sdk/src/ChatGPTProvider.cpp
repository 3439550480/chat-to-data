#include "../include/ai_chat_sdk/ChatGPTProvider.h"
#include "../include/ai_chat_sdk/util/myLog.h"
#include <cstdint>
#include <jsoncpp/json/json.h>
#include <httplib.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <sstream>


namespace ai_chat_sdk{

// 初始化模型
bool ChatGPTProvider::initModel(const std::map<std::string, std::string>& modelConfig){
    // 初始化API Key
    auto it = modelConfig.find("api_key");
    if(it == modelConfig.end()){
        ERR("ChatGPTProvider initModel api_key not found");
        return false;
    }else{
        _apiKey = it->second;
    }

    // 初始化Base URL
    it = modelConfig.find("endpoint");
    if(it == modelConfig.end()){
        _endpoint = "https://api.openai.com";
    }else{
        _endpoint = it->second;
    }
        
    _isAvailable = true;
    INFO("ChatGPTProvider initModel success, endpoint: {}", _endpoint);
    return true;
}

// 检测模型是否可用
bool ChatGPTProvider::isAvailable() const{
    return _isAvailable;
}

// 获取模型名称
std::string ChatGPTProvider::getModelName() const{
    return "gpt-4o-mini";
}

// 获取模型描述
std::string ChatGPTProvider::getModelDesc() const{
    return "OpenAI 推出的轻量级、高性价比模型，核心能力解决 GPT-4 Turbo，但更经济";
}

// 发送消息 - 全量返回
std::string ChatGPTProvider::sendMessage(const std::vector<Message>& messages, const std::map<std::string, std::string>& requestParam){
    // 1. 检测模型是否可用
    if(!isAvailable()){
        ERR("ChatGPTProvider sendMessage model not available");
        return "";
    }

    // 2. 构造请求参数：模型名称、消息列表、温度值、maxtoen数、是否开启流式响应(默认未开启)
    double temperature = 0.7;
    int maxOutputTokens = 2048;
    if(requestParam.find("temperature") != requestParam.end()){
        temperature = std::stod(requestParam.at("temperature"));
    }
    if(requestParam.find("max_output_tokens") != requestParam.end()){
        maxOutputTokens = std::stoi(requestParam.at("max_output_tokens"));
    }

    // 构建消息列表
    Json::Value messagesArray(Json::arrayValue);
    for(const auto& msg : messages){
        Json::Value messageJson(Json::objectValue);
        messageJson["role"] = msg._role;
        messageJson["content"] = msg._content;
        messagesArray.append(messageJson);
    }

    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["input"] = messagesArray;
    requestBody["temperature"] =temperature;
    requestBody["max_output_tokens"] = maxOutputTokens;

    // 3. 序列化
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    // 4. 创建HTTP客户端
    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(30, 0);
    client.set_read_timeout(60, 0);
    client.set_proxy("127.0.0.1", 7890);

    // 设置请求头参数
    httplib::Headers headers = {
        {"Authorization", "Bearer " + _apiKey}
        //{"Content-Type", "application/json"}
    };

    // 5. 发送POST请求
    auto response = client.Post("/v1/responses", headers, requestBodyStr, "application/json");
    if(!response){
        ERR("ChatGPTProvider sendMessage POST request failed, error: {}", to_string(response.error()));
        return "";
    }

    // 6. 检测响应是否成功
    if(response->status != 200){
        ERR("ChatGPTProvider sendMessage POST request failed, status: {}", response->status);
        return "";
    }

    INFO("ChatGPTProvider API reponse body : {}", response->body);

    // 7. 对模型返回结构进行序列化
    Json::CharReaderBuilder reader;
    std::istringstream responseStream(response->body);
    std::string errorJson;
    Json::Value responseJson;
    if(!Json::parseFromStream(reader, responseStream, &responseJson, &errorJson)){
        ERR("ChatGPTProvider sendMessage parse response body failed");
        return "";
    }

    // 8. 从响应体中提取模型返回的消息内容
    if(responseJson.isMember("output") && responseJson["output"].isArray() && !responseJson["output"].empty()){
        // 模型的回复刚好是output数组的第0个元素
        auto output = responseJson["output"][0];
        if(output.isMember("content") && output["content"].isArray() && !output["content"].empty() && output["content"][0].isMember("text")){
            std::string replyString = output["content"][0]["text"].asString();
            INFO("ChatGPTProvider sendMessage replyString: {}", replyString);
            return replyString;
        }
    }

    // 9. 处理模型返回结构异常情况
    ERR("ChatGPTProvider sendMessage parse response body failed, errorJson: {}", errorJson);
    return "";
}

// 发送消息 - 增量返回 - 流式响应
std::string ChatGPTProvider::sendMessageStream(const std::vector<Message>& messages, 
                                              const std::map<std::string, std::string>& requestParam,
                                              std::function<void(const std::string&, bool)> callback){
    // 检测模型是否可用
    if(!isAvailable()){
        ERR("ChatGPTProvider sendMessageStream model not available");
        return "";
    }

    // 构造请求参数
    double temperature = 0.7;
    int maxOutputTokens = 2048;
    if(requestParam.find("temperature") != requestParam.end()){
        temperature = std::stod(requestParam.at("temperature"));
    }
    if(requestParam.find("max_output_tokens") != requestParam.end()){
        maxOutputTokens = std::stoi(requestParam.at("max_output_tokens"));
    }

    // 构建消息列表
    Json::Value messagesArray(Json::arrayValue);
    for(const auto& msg : messages){
        Json::Value messageJson(Json::objectValue);
        messageJson["role"] = msg._role;
        messageJson["content"] = msg._content;
        messagesArray.append(messageJson);
    }

    // 构建请求体
    Json::Value requestBody;
    requestBody["model"] = getModelName();
    requestBody["input"] = messagesArray;
    requestBody["temperature"] =temperature;
    requestBody["max_output_tokens"] = maxOutputTokens;
    requestBody["stream"] = true;

    // 3. 序列化
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);

    // 4. 创建HTTP客户端
    httplib::Client client(_endpoint.c_str());
    client.set_connection_timeout(60, 0);
    client.set_read_timeout(300, 0);
    client.set_proxy("127.0.0.1", 7890);

    // 设置请求头参数
    httplib::Headers headers = {
        {"Authorization", "Bearer " + _apiKey},
        {"Content-Type", "application/json"},
        {"Accept", "text/event-stream"},
    };

    // 流式处理相关变量
    std::string buffer;
    bool gotError = false;
    std::string errorMsg;
    int statusCode = 0;
    bool streamFinish = false;   // false 表示流式响应未结束
    std::string fullResponse;    // 累积完整的数据

    // 创建请求对象
    httplib::Request request;
    request.method = "POST";
    request.path = "/v1/responses";
    request.body = requestBodyStr;
    request.headers = headers;

    // 设置相应处理器
    request.response_handler = [&](const httplib::Response& res){
        statusCode = res.status;
        if(statusCode != 200){
            gotError = true;
            errorMsg = "ChatGPTProvider sendMessageStream POST request failed, status: " + std::to_string(statusCode);
            return false;    // 终止请求
        }
        return true;   // 继续接收后续数据
    };

    // 设置内容接收处理器
    request.content_receiver = [&](const char* data, size_t dataLength, size_t offset, size_t totalLength){
        // 如果HTTP请求失败，不用继续接收后续数据
        if(gotError){
            return false;    // 终止接收后续数据
        }

        buffer.append(data, dataLength);

        INFO("ChatGPTProvider sendMessageStream received data: {}", buffer);

        // 检查是否收到了完整的事件流数据
        size_t pos = 0;
        while((pos = buffer.find("\n\n", pos)) != std::string::npos){
            std::string event = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);   // 移除已处理的数据

            // 解析事件类型和具体数据的位置
            std::istringstream eventStram(event);
            std::string eventType;
            std::string eventData;
            std::string line;
            while(std::getline(eventStram, line)){
                if(line.empty()){
                    continue;
                }
                if(line.compare(0, 6, "event:") == 0){
                    eventType = line.substr(7);     // 注意：要将event:之后的空格不要截取到事件类型中，否则会影响后续事件的检测
                }else if(line.compare(0, 5, "data:") == 0){
                    eventData = line.substr(6);
                }
            }

            INFO("ChatGPTProvider sendMessageStream eventType: {}, eventData: {}", eventType, eventData);

            // 对模型返回的结果进行序列化
            Json::Value chunk;
            Json::CharReaderBuilder reader;
            std::string errs;
            std::istringstream eventDataStream(eventData);
            if(!Json::parseFromStream(reader, eventDataStream, &chunk, &errs)){
                ERR("ChatGPTProvider sendMessageStream parse response body failed, error: {}", errs);
                continue;
            }

            // 按照事件类型进行数据分析
            if(eventType == "response.output_text.delta"){
                if(chunk.isMember("delta") && chunk["delta"].isString()){
                    std::string delta = chunk["delta"].asString();
                    callback(delta, false);
                }
            } else if(eventType == "response.output_item.done"){
                // 表示一个output_item传输结束，需要将数据拼接到
                if(chunk.isMember("item") && chunk["item"].isObject()){
                    Json::Value item = chunk["item"];
                    if(item.isMember("content") && item["content"].isArray() && !item["content"].empty() && item["content"][0].isMember("text") && item["content"][0]["text"].isString()){
                        fullResponse += item["content"][0]["text"].asString();
                    }
                }
            }else if(eventType == "response.completed"){
                streamFinish = true;
                callback("", true);
                return true;
            }
        }

        return true;   // 继续接收后续数据
    };

    auto result = client.send(request); 
    if(!result){
        ERR("ChatGPTProvider sendMessageStream POST request failed, error: {}", to_string(result.error()));
        return "";
    }

    // 确保流式操作正确结束
    if(!streamFinish){
        WARN("stream ended without response.completed");
        callback("", true);
        return "";
    }
    return fullResponse;
}

}// end ai_chat_sdk
