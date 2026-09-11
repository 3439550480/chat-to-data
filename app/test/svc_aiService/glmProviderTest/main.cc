// H1.5 冒烟测试：验证 SDK 补丁（GLM 接入）与 .env 读取
// 两阶段设计：
//   阶段 A（离线，总是执行）：用占位 key 初始化 → 断言 GLM 模型已注册且可用、
//                              模型名/描述/endpoint 配置链路正确（不打网络）
//   阶段 B（在线，仅当 GLM_API_KEY 非空时执行）：真实调用 GLM，验证 SSE 流式回复
// 运行：./glmProviderTest            仅阶段 A
//       ./glmProviderTest --live     阶段 A + B（需 .env 里填好 key）
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <ai_chat_sdk/ChatSDK.h>
#include <ai_chat_sdk/util/myLog.h>
#include <bite_scaffold/log.h>
#include "../../../common/envLoader.h"

static int g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            INF("[OK]   {}", msg);                                              \
        } else {                                                                \
            ERR("[FAIL] {}", msg);                                              \
            ++g_fail;                                                           \
        }                                                                       \
    } while (0)

// 依次尝试若干候选路径，找到 .env
static std::string findEnvFile() {
    const char* candidates[] = {
        ".env", "../.env", "../../.env", "../../../.env", "../../../../.env",
        "/home/dev/chat2data/.env"
    };
    for (const char* path : candidates) {
        std::ifstream ifs(path);
        if (ifs.is_open()) {
            return path;
        }
    }
    return "";
}

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    // SDK 内部使用 bite::Logger 打日志，未初始化时 getLogger() 返回空指针
    // → 第一次 INFO 即段错误。必须在使用 SDK 前初始化（课件 main.cc 同样如此）
    bite::Logger::initLogger("glmProviderTest", "stdout", spdlog::level::info);
    bool live = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--live") { live = true; }
    }

    // ===== 0. 加载 .env =====
    std::string envPath = findEnvFile();
    if (!envPath.empty()) {
        chat2Data::loadEnvFile(envPath);
        INF("using env file: {}", envPath);
    } else {
        WRN("no .env found, fallback to process environment");
    }
    std::string apiKey = chat2Data::getEnvOrDefault("GLM_API_KEY");
    std::string modelName = chat2Data::getEnvOrDefault("GLM_MODEL_NAME", "glm-4-flash");
    std::string baseUrl = chat2Data::getEnvOrDefault("GLM_BASE_URL",
                                                     "https://open.bigmodel.cn/api/paas/v4");
    std::string modelDesc = chat2Data::getEnvOrDefault("GLM_MODEL_DESC", "GLM 通用对话模型");

    // ===== 阶段 A：离线结构验证 =====
    INF("========== phase A: offline registration check ==========");
    {
        ai_chat_sdk::ChatSDK sdk;
        std::vector<std::shared_ptr<ai_chat_sdk::Config>> configs;
        auto apiConfig = std::make_shared<ai_chat_sdk::APIConfig>();
        apiConfig->_modelName = modelName;
        apiConfig->_apiKey = apiKey.empty() ? "placeholder-for-offline-test" : apiKey;
        apiConfig->_baseUrl = baseUrl;
        apiConfig->_modelDesc = modelDesc;
        apiConfig->_temperature = 0.7;
        apiConfig->_maxTokens = 2048;
        configs.push_back(apiConfig);

        bool initOk = sdk.initModels(configs);
        CHECK(initOk, "ChatSDK::initModels returns true");

        auto models = sdk.getAvailableModels();
        bool found = false;
        bool available = false;
        std::string gotDesc;
        for (const auto& m : models) {
            if (m._modelName == modelName) {
                found = true;
                available = m._isAvailable;
                gotDesc = m._modelDesc;
                INF("registered model: name={}, desc={}, provider={}, endpoint={}, available={}",
                    m._modelName, m._modelDesc, m._provider, m._endpoint, m._isAvailable);
            }
        }
        CHECK(found, "GLM model registered in ChatSDK (patch effective)");
        CHECK(available, "GLM model marked as available");
        CHECK(gotDesc == modelDesc, "model desc plumbed through config");

        // 会话创建（ChatSDK 本地 sqlite：chatDB.db）
        std::string sessionId = sdk.createSession(modelName);
        CHECK(!sessionId.empty(), "createSession returns session id");
        auto session = sdk.getSession(sessionId);
        CHECK(session != nullptr, "getSession returns session");
        if (session) {
            CHECK(session->_modelName == modelName, "session bound to GLM model");
        }
        if (!sessionId.empty()) {
            CHECK(sdk.deleteSession(sessionId), "deleteSession works");
        }
    }

    // ===== 阶段 B：真实调用（需 key）=====
    if (!live) {
        INF("========== phase B skipped (use --live to enable) ==========");
    } else if (apiKey.empty()) {
        WRN("========== phase B skipped: GLM_API_KEY is empty in .env ==========");
    } else {
        INF("========== phase B: live call to GLM ==========");
        ai_chat_sdk::ChatSDK sdk;
        std::vector<std::shared_ptr<ai_chat_sdk::Config>> configs;
        auto apiConfig = std::make_shared<ai_chat_sdk::APIConfig>();
        apiConfig->_modelName = modelName;
        apiConfig->_apiKey = apiKey;
        apiConfig->_baseUrl = baseUrl;
        apiConfig->_modelDesc = modelDesc;
        configs.push_back(apiConfig);   // ★ 此前遗漏：configs 为空导致 initModels 未登记 GLM
        sdk.initModels(configs);
        std::string sessionId = sdk.createSession(modelName);
        CHECK(!sessionId.empty(), "live: session created");

        std::string full;
        std::string reply = sdk.sendMessageStream(
            sessionId, "请只回复四个字：连接成功",
            [&full](const std::string& chunk, bool done) {
                full += chunk;
                if (done) {
                    INF("live: stream finished");
                }
                return true;
            });
        INF("live reply (accumulated by callback): {}", full);
        INF("live reply (returned by SDK): {}", reply);
        CHECK(!reply.empty(), "live: GLM returns non-empty reply (SSE works)");
        sdk.deleteSession(sessionId);
    }

    INF("========== summary: failCount={} ==========", g_fail);
    return g_fail == 0 ? 0 : 1;
}
