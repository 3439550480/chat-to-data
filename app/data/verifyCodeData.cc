#include "verifyCodeData.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <bite_scaffold/redis.h>

namespace userService {

VerifyCodeData::VerifyCodeData(std::shared_ptr<sw::redis::Redis> redis)
    : _redis(redis) {
    INF("VerifyCodeData initialized");
}

bool VerifyCodeData::saveVerifyCodeToCache(const VerifyCodeInfo& verifyCodeInfo) {
    try {
        // 1. 生成缓存 key（单键：verifyCode:codeId）
        std::string key = "verifyCode:" + verifyCodeInfo._codeId;

        // 2. 序列化验证码信息（4 字段，与 getVerifyCodeFromCache 解析处严格一致）
        Json::Value codeJson;
        codeJson["codeId"] = verifyCodeInfo._codeId;
        codeJson["verifyCode"] = verifyCodeInfo._verifyCode;
        codeJson["email"] = verifyCodeInfo._email;
        codeJson["createTime"] = verifyCodeInfo._createTime;

        auto jsonStr = biteutil::JSON::serialize(codeJson);
        if (!jsonStr.has_value()) {
            WRN("Failed to serialize verify code info for cache");
            return false;
        }

        // 3. 锁内 setex：写值 + 5min TTL 一步完成
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            _redis->setex(key, VERIFY_CODE_CACHE_TTL_SECONDS, jsonStr.value());
        }
        INF("Verify code saved to cache: codeId={}, email={}", verifyCodeInfo._codeId, verifyCodeInfo._email);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save verify code to cache: codeId={}, error={}", verifyCodeInfo._codeId, e.what());
        return false;
    }
}

std::optional<VerifyCodeInfo> VerifyCodeData::getVerifyCodeFromCache(const std::string& codeId) {
    try {
        std::string key = "verifyCode:" + codeId;
        std::string jsonStr;
        {
            // 锁内 GET 拷值出锁，反序列化放锁外（同 UserData/SessionData 锁边界纪律）
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("Verify code not found in cache: codeId={}", codeId);
                return std::nullopt;
            }
            jsonStr = result.value();
        }
        auto jsonOpt = biteutil::JSON::unserialize(jsonStr);
        if (!jsonOpt.has_value()) {
            WRN("Failed to unserialize verify code from cache: codeId={}", codeId);
            return std::nullopt;
        }
        Json::Value codeJson = jsonOpt.value();
        VerifyCodeInfo verifyCodeInfo;
        verifyCodeInfo._codeId = codeJson["codeId"].asString();
        verifyCodeInfo._verifyCode = codeJson["verifyCode"].asString();
        verifyCodeInfo._email = codeJson["email"].asString();
        verifyCodeInfo._createTime = codeJson["createTime"].asString();
        INF("Verify code found in cache: codeId={}", codeId);
        return verifyCodeInfo;
    } catch (const std::exception& e) {
        ERR("Failed to get verify code from cache: codeId={}, error={}", codeId, e.what());
        return std::nullopt;
    }
}

bool VerifyCodeData::deleteVerifyCodeFromCache(const std::string& codeId) {
    try {
        std::string key = "verifyCode:" + codeId;
        std::lock_guard<std::mutex> lock(_redisMutex);
        auto res = _redis->del(key);
        if (res == 0) {
            // 与 SessionData 的幂等 true 不同：验证码"用后即删"，key 不存在
            // = 验证码已过期或已使用 → 业务层视作"验证码无效"信号，返回 false
            INF("Verify code not found in cache: codeId={}", codeId);
            return false;
        }
        INF("Verify code deleted from cache: codeId={}", codeId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to delete verify code from cache: codeId={}, error={}", codeId, e.what());
        return false;
    }
}

} // namespace userService
