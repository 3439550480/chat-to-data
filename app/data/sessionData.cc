#include "sessionData.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <odb/mysql/database.hxx>
#include <odb/transaction.hxx>
#include <bite_scaffold/redis.h>
#include <bite_scaffold/odb.h>
#include "sessionEntity.h"
#include "odb/sessionEntity-odb.hxx"

namespace userService {

SessionData::SessionData(std::shared_ptr<odb::database> db,
                         std::shared_ptr<sw::redis::Redis> redis)
    : _db(db)
    , _redis(redis) {
    // 补充：与 UserData 对齐，装配成功信号便于启动日志排查注入链路是否走通
    INF("SessionData initialized");
}
// Cache-Aside 的标准形态：

// 写路径：写 DB，不碰缓存（新建的 key 本来就不存在，连"删缓存"都不需要）
// 读路径：先缓存 → 未命中查 DB → 回填缓存（getUserIdBySessionId 里就是这个三段式）
bool SessionData::saveSessionToDb(const SessionInfo& sessionInfo) {
    try {
        // 1. 构造实体（tbl_session 无业务判断字段，直构：sessionId/userId 与结构体字段序一致）
        SessionEntity session(sessionInfo._sessionId, sessionInfo._userId);
        // 2. 开启事务
        odb::transaction trans(_db->begin());
        // 3. 插入会话信息：INSERT INTO tbl_session (sessionId, userId) VALUES (...)
        //    与 UserData 不同：无"查→改/插"二分——会话每次都是全新 UUID，不存在更新场景，
        //    sessionId UNIQUE 约束兜底，重复插入抛约束异常走 catch
        _db->persist(session);
        // 4. 提交事务
        trans.commit();

        INF("Session saved to DB: sessionId={}, userId={}", sessionInfo._sessionId, sessionInfo._userId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save session to DB: sessionId={}, error={}", sessionInfo._sessionId, e.what());
        return false;
    }
}

std::optional<SessionInfo> SessionData::getSessionBySessionIdFromDb(const std::string& sessionId) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 查询会话信息：SELECT * FROM tbl_session WHERE sessionId = ?
        //    query_one 实测返回裸指针（本 ODB 版本），unique_ptr 接管防泄漏（问题⑩，同 UserData）
        auto result = std::unique_ptr<SessionEntity>(
            _db->query_one<SessionEntity>(odb::query<SessionEntity>::sessionId == sessionId));
        // 3. 提交事务
        trans.commit();
        // 4. 返回结果
        if (result) {
            SessionInfo sessionInfo;
            sessionInfo._sessionId = result->sessionId();
            sessionInfo._userId = result->userId();
            INF("Session found in DB: sessionId={}", sessionId);
            return sessionInfo;
        }
        INF("Session not found in DB: sessionId={}", sessionId);   // WRN→INF：鉴权 miss 是常态（TTL 过期后首次请求），非异常
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get session from DB: sessionId={}, error={}", sessionId, e.what());
        return std::nullopt;
    }
}

bool SessionData::deleteSessionFromDb(const std::string& sessionId) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 删除会话信息：DELETE FROM tbl_session WHERE sessionId = ?
        //    erase_query 按条件批量删除，无需先 query_one 拿实体（退出登录只有一个目的地）
        _db->erase_query<SessionEntity>(odb::query<SessionEntity>::sessionId == sessionId);
        // 3. 提交事务
        trans.commit();

        INF("Session deleted from DB: sessionId={}", sessionId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to delete session from DB: sessionId={}, error={}", sessionId, e.what());
        return false;
    }
}

bool SessionData::saveSessionToCache(const SessionInfo& sessionInfo) {
    try {
        // 1. 生成缓存 key（单键：session:sessionId —— 会话查询入口只有 sessionId）
        std::string key = "session:" + sessionInfo._sessionId;

        // 2. 序列化会话信息（2 字段，JSON 名与 getSessionFromCache 解析处严格一致）
        Json::Value sessionJson;
        sessionJson["sessionId"] = sessionInfo._sessionId;
        sessionJson["userId"] = sessionInfo._userId;
        auto jsonStr = biteutil::JSON::serialize(sessionJson);
        if (!jsonStr.has_value()) {
            WRN("Failed to serialize session info for cache");
            return false;
        }

        // 3. 锁内 setex：写值 + 3 天 TTL 一步完成（会话是登录凭证，用户 3 天内免登录）
        std::lock_guard<std::mutex> lock(_redisMutex);
        _redis->setex(key, SESSION_CACHE_TTL_SECONDS, jsonStr.value());

        INF("Session saved to cache: sessionId={}", sessionInfo._sessionId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save session to cache: sessionId={}, error={}", sessionInfo._sessionId, e.what());
        return false;
    }
}

std::optional<SessionInfo> SessionData::getSessionFromCache(const std::string& sessionId) {
    try {
        std::string key = "session:" + sessionId;
        std::string jsonStr;
        {
            // 锁内 GET 拷值出锁，反序列化放锁外（同 UserData 的锁边界纪律）
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("Session not found in cache: sessionId={}", sessionId);
                return std::nullopt;   // 上层回源 DB 并回填（SessionManager 三段式）
            }
            jsonStr = result.value();
        }
        auto jsonOpt = biteutil::JSON::unserialize(jsonStr);
        if (!jsonOpt.has_value()) {
            WRN("Failed to unserialize session from cache: sessionId={}", sessionId);   // 脏缓存告警
            return std::nullopt;
        }

        SessionInfo sessionInfo;
        sessionInfo._sessionId = jsonOpt.value()["sessionId"].asString();
        sessionInfo._userId = jsonOpt.value()["userId"].asString();

        INF("Session found in cache: sessionId={}", sessionId);
        return sessionInfo;
    } catch (const std::exception& e) {
        ERR("Failed to get session from cache: sessionId={}, error={}", sessionId, e.what());
        return std::nullopt;
    }
}

bool SessionData::deleteSessionFromCache(const std::string& sessionId) {
    try {
        std::string key = "session:" + sessionId;
        std::lock_guard<std::mutex> lock(_redisMutex);
        auto res = _redis->del(key);
        if (res == 0) {
            // key 本就不存在（TTL 已过期/重复登出）：幂等成功，记 INF 不算错误
            INF("Session not found in cache: sessionId={}", sessionId);
            return true;
        }

        INF("Session deleted from cache: sessionId={}", sessionId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to delete session from cache: sessionId={}, error={}", sessionId, e.what());
        return false;
    }
}

} // namespace userService
