#pragma once
#include <string>
#include <memory>
#include "../data/sessionData.h"

namespace userService {

// 会话业务操作类：使用 SessionData 完成会话的创建、删除、查询
// 异常约定：数据操作失败抛 chat2Data::Chat2DataException（错误码见 errorHandler.h）
class SessionManager {
public:
    explicit SessionManager(SessionData* sessionData);

    // 新建会话（生成 sessionId 并落库）
    // @return 新会话的 sessionId
    // @throws USER_SESSION_CREATE_ERROR 会话保存失败
    std::string createSession(const std::string& userId);

    // 删除会话（DB + 缓存双删）
    // @throws USER_SESSION_DELETE_ERROR 任一删除失败
    bool deleteSession(const std::string& sessionId);

    // 根据会话id获取用户id（缓存优先，未命中回源 DB 并回填缓存）
    // @throws USER_SESSION_CACHE_ERROR 回填缓存失败
    // @throws USER_SESSION_INVALID 会话不存在/已过期
    std::string getUserIdBySessionId(const std::string& sessionId);

private:
    SessionData* _sessionData;   // 会话数据操作指针（借用语义：不拥有，装配层持有所有权）
};

} // namespace userService
