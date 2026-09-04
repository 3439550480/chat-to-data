#pragma once

#include <memory>
#include <string>
#include <optional>
#include <mutex>
#include <odb/mysql/database.hxx>
#include <sw/redis++/redis.h>
#include "../svc_userService/common.h"

namespace userService {

// 会话表数据操作类：封装对 tbl_session 的数据库与 Redis 缓存双向存取
// 与 UserData 同构但缓存为单键(session:sessionId)：会话没有"多身份"问题
class SessionData {
public:
    // 构造函数，注入外部创建的 MySQL 与 Redis 连接（依赖注入，同 UserData）
    SessionData(std::shared_ptr<odb::database> db,
                std::shared_ptr<sw::redis::Redis> redis);

    // 保存会话信息到MySQL
    bool saveSessionToDb(const SessionInfo& sessionInfo);

    // 从MySQL通过会话ID获取会话信息
    std::optional<SessionInfo> getSessionBySessionIdFromDb(const std::string& sessionId);

    // 从MySQL删除指定会话信息
    bool deleteSessionFromDb(const std::string& sessionId);

    // 保存会话信息到Redis缓存
    bool saveSessionToCache(const SessionInfo& sessionInfo);

    // 从Redis缓存通过会话ID获取会话信息
    std::optional<SessionInfo> getSessionFromCache(const std::string& sessionId);

    // 从Redis缓存删除指定会话信息
    bool deleteSessionFromCache(const std::string& sessionId);

private:
    std::shared_ptr<odb::database> _db;        // MySQL数据库连接
    std::shared_ptr<sw::redis::Redis> _redis;  // Redis数据库连接
    std::mutex _redisMutex;                    // Redis操作互斥锁
    const static int SESSION_CACHE_TTL_SECONDS = 3 * 24 * 60 * 60;  // 会话缓存过期时间为3天
};

} // namespace userService
