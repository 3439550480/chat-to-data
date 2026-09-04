#pragma once

#include <string>
#include <cstddef>
#include <odb/core.hxx>

// 指定该类映射到数据库中的tbl_session表
#pragma db object table("tbl_session")
class SessionEntity {
    public:
        SessionEntity() {}
        SessionEntity(const std::string& sessionId, const std::string& userId)
            : _sessionId(sessionId), _userId(userId) {}

        unsigned long long id() const { return _id; }
        void setId(unsigned long long id) { _id = id; }

        std::string sessionId() const { return _sessionId; }
        void setSessionId(const std::string& sessionId) { _sessionId = sessionId; }

        std::string userId() const { return _userId; }
        void setUserId(const std::string& userId) { _userId = userId; }

    private:
        friend class odb::access;

        // 主键自增
        #pragma db id auto
        unsigned long long _id;

        #pragma db unique
        #pragma db column("sessionId") type("VARCHAR(32) CHARACTER SET utf8mb4")
        std::string _sessionId;

        #pragma db column("userId") type("VARCHAR(32) CHARACTER SET utf8mb4")
        std::string _userId;
};
