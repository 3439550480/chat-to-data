# ai_chat_sdk 补丁说明（本项目对第三方 SDK 的改动）

- **来源**：课程环境镜像内 `/home/dev/openPrjSource/AIModelAcessTech/sdk`（ChatSDK，第三方库）
- **复制原因**：镜像内源码目录不受本仓库管理（镜像重建即丢失、改动不可追溯），故复制进项目后以补丁方式维护
- **复制日期**：2026-09-11
- **原始 CMakeLists**：见本目录 `CMakeLists.orig.txt`

## 补丁清单

| 编号 | 文件 | 内容 | 原因 |
|---|---|---|---|
| PATCH 0 | `CMakeLists.txt` | 去掉 `install(...)` 到 `/usr/local`，改为项目内静态库（`add_subdirectory` 后链接 `ai_chat_sdk`） | 不污染系统目录、不依赖镜像环境 |
| PATCH 1 | `include/ai_chat_sdk/common.h` | `APIConfig` 增加 `_baseUrl`、`_modelDesc` 两个可选字段 | 原实现只有 `_apiKey`，endpoint 与模型名在各 Provider 内**硬编码**，无法接入需自定义 baseUrl/模型名的 OpenAI 兼容服务（GLM） |
| PATCH 2 | `include/ai_chat_sdk/GLMProvider.h`、`src/GLMProvider.cpp`（新增） | 新增 GLM Provider：OpenAI 兼容 `POST {baseUrl}/chat/completions` + SSE（`data:` / `delta.content` / `[DONE]`），模型名 / apiKey / baseUrl / 描述全部来自配置 | 复用 DeepSeekProvider 的 SSE 解析逻辑，但去掉硬编码 |
| PATCH 3 | `src/ChatSDK.cpp` | ① `registerAllProvider`：`APIConfig` 带非空 `_baseUrl` 时注册 `GLMProvider`；② `initProviders`：带 `_baseUrl` 的 API 配置走可配 Provider 分支（原 deepseek/gpt/gemini 判断保留）；③ `initAPIModelProviders`：带 `_baseUrl` 时把 `endpoint`/`model_name`/`model_desc` 一并下传 | 打通「配置 → Provider 初始化 → 请求体 model 字段」链路 |
| 附 | `include/`、`src/` 目录重排为安装布局（`include/ai_chat_sdk/*.h`），并把 `src/*.cpp` 的 `../include/X.h` 改为 `../include/ai_chat_sdk/X.h` | 使课件代码的 `#include <ai_chat_sdk/ChatSDK.h>` 原样可用，且优先于系统 `/usr/local/include/ai_chat_sdk`（旧版无 `_baseUrl`） |

## 使用方式（本项目）

```cpp
#include <ai_chat_sdk/ChatSDK.h>
#include <ai_chat_sdk/util/myLog.h>     // 必须先 initLogger，否则 SDK 内 INFO 宏空指针崩溃

bite::Logger::initLogger("aiService", "stdout", spdlog::level::info);   // ← 前置条件
ai_chat_sdk::ChatSDK sdk;
auto cfg = std::make_shared<ai_chat_sdk::APIConfig>();
cfg->_modelName = getEnv("GLM_MODEL_NAME");    // 任意的 GLM 模型名
cfg->_apiKey    = getEnv("GLM_API_KEY");
cfg->_baseUrl   = getEnv("GLM_BASE_URL");      // 非空即走 GLMProvider
cfg->_modelDesc = getEnv("GLM_MODEL_DESC");    // 仅展示用，可空
sdk.initModels({cfg});
```

## 兼容性

- 原 `deepseek-chat` / `gpt-4o-mini` / `gemini-2.0-flash` 三个云端模型与 Ollama 本地模型路径**零改动**；
- 仅当 `APIConfig::_baseUrl` 非空时才启用新分支，原有用法（只填 `_apiKey`）行为不变。

## 已知观察项（非本项目引入）

- `LLMManager::registerProvider` 构造 `ModelInfo(modelName)` 时不填 `_provider`/`_endpoint`，故 `getAvailableModels()` 返回的这两字段为空（SDK 原行为；我们 `GetModels` 接口只用 name/desc，无影响）。
- SDK 内部日志依赖 `bite::Logger::initLogger` 先被调用（否则空指针崩溃）——所有使用者必须遵守，已在服务 `main.cc` 与测试里显式初始化。
