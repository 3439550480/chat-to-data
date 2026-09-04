#pragma once

#include <memory>
#include <string>
#include <optional>
#include <mutex>
#include <sw/redis++/redis.h>
#include "../svc_userService/common.h"

namespace userService {

// 验证码数据操作类：纯 Redis 存取（验证码生命周期仅 5min，不入 MySQL）
class VerifyCodeData {
public:
    // 构造函数：只注入 Redis 连接（验证码不落库）
    explicit VerifyCodeData(std::shared_ptr<sw::redis::Redis> redis);

    // 保存验证码信息到Redis缓存
    bool saveVerifyCodeToCache(const VerifyCodeInfo& verifyCodeInfo);

    // 从Redis缓存获取验证码信息
    std::optional<VerifyCodeInfo> getVerifyCodeFromCache(const std::string& codeId);

    // 从Redis缓存删除验证码信息（用后即删；key 不存在返回 false = 验证码已过期/已用）
    bool deleteVerifyCodeFromCache(const std::string& codeId);

private:
    std::shared_ptr<sw::redis::Redis> _redis;
    std::mutex _redisMutex;
    const static int VERIFY_CODE_CACHE_TTL_SECONDS = 5 * 60;  // 验证码缓存过期时间为5分钟
};

} // namespace userService
