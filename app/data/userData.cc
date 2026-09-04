#include "userData.h"
#include <bite_scaffold/log.h>
#include <bite_scaffold/util.h>
#include <odb/mysql/database.hxx>
#include <odb/transaction.hxx>
#include <bite_scaffold/redis.h>
#include <bite_scaffold/odb.h>
#include "userEntity.h"
#include "odb/userEntity-odb.hxx"

namespace userService {

UserData::UserData(std::shared_ptr<odb::database> db,
                   std::shared_ptr<sw::redis::Redis> redis)
    : _db(db)
    , _redis(redis) {
    INF("UserData initialized");
}

// 双模型唯一映射点：UserEntity(带自增主键/ODB注解) → UserInfo(业务纯数据)
// 新增加字段时只改这里；课件原版在 6 处查询中重复 5 行赋值，漏改即静默丢数据
UserInfo UserData::fromEntity(const UserEntity& entity) {
    UserInfo userInfo;
    userInfo._userId = entity.userId();
    userInfo._nickname = entity.nickname();
    userInfo._password = entity.password();
    userInfo._email = entity.email();
    userInfo._status = static_cast<UserStatus>(entity.status());
    return userInfo;
}

/**
 * @brief 保存用户信息到 MySQL（存在则更新，不存在则插入）
 * @note 契约1: _password 必须已是加密后的密文，本层不做任何加密处理
 * @note 契约2: 返回 false 仅表示数据库操作异常（连接失败/约束冲突等）；
 *              "查无此人"属正常业务结果，请走 query 系列接口(返回 std::nullopt)
 * @note 并发说明: userId/nickname/email 均有 UNIQUE 约束兜底，极端并发下
 *              重复插入会抛异常并由下方 catch 捕获返回 false，不会产生脏数据
 */
bool UserData::saveUserToDb(const UserInfo& userInfo) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());

        // 2. 查询用户是否已存在（以 userId 为判据）
        //    query_one 实测返回裸指针（本 ODB 版本），unique_ptr 接管防泄漏
        auto existingUser = std::unique_ptr<UserEntity>(
            _db->query_one<UserEntity>(odb::query<UserEntity>::userId == userInfo._userId));

        if (existingUser) {
            // 3a. 用户已存在，执行更新（保留 id 主键，只刷业务字段）
            existingUser->setNickname(userInfo._nickname);
            existingUser->setEmail(userInfo._email);
            existingUser->setPassword(userInfo._password);
            existingUser->setStatus(static_cast<unsigned char>(userInfo._status));
            _db->update(*existingUser);
            INF("User updated in DB: userId={}, nickname={}", userInfo._userId, userInfo._nickname);
        } else {
            // 3b. 用户不存在，执行插入
            UserEntity user(userInfo._userId, userInfo._nickname, userInfo._email,
                           userInfo._password, static_cast<unsigned char>(userInfo._status));
            _db->persist(user);
            INF("User saved to DB: userId={}, nickname={}", userInfo._userId, userInfo._nickname);
        }

        // 4. 提交事务
        trans.commit();
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save user to DB: userId={}, error={}", userInfo._userId, e.what());
        return false;
    }
}

std::optional<UserInfo> UserData::getUserByUserIdFromDb(const std::string& userId) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 查询用户信息：SELECT * FROM tbl_user WHERE userId = ?
        //    query_one 实测返回裸指针（本 ODB 版本），unique_ptr 接管防泄漏
        auto result = std::unique_ptr<UserEntity>(
            _db->query_one<UserEntity>(odb::query<UserEntity>::userId == userId));
        // 3. 提交事务
        trans.commit();
        // 4. 返回结果
        if (result) {
            INF("User found in DB by userId: userId={}", userId);
            return fromEntity(*result);
        }
        INF("User not found in DB by userId: userId={}", userId);   // WRN→INF：未找到是正常业务流（注册预检），非异常
        return std::nullopt;
    } catch (const std::exception& e) {
        // 已知取舍：DB 异常此处吞掉返回 nullopt，业务层按业务场景解释（教学简化，生产应改 expected<T>）
        ERR("Failed to get user from DB by userId: userId={}, error={}", userId, e.what());
        return std::nullopt;
    }
}

std::optional<UserInfo> UserData::getUserByNicknameFromDb(const std::string& nickname) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 查询用户信息：SELECT * FROM tbl_user WHERE nickname = ?
        auto result = std::unique_ptr<UserEntity>(
            _db->query_one<UserEntity>(odb::query<UserEntity>::nickname == nickname));
        // 3. 提交事务
        trans.commit();
        // 4. 返回结果
        if (result) {
            INF("User found in DB by nickname: nickname={}", nickname);
            return fromEntity(*result);
        }
        INF("User not found in DB by nickname: nickname={}", nickname);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get user from DB by nickname: nickname={}, error={}", nickname, e.what());
        return std::nullopt;
    }
}

std::optional<UserInfo> UserData::getUserByEmailFromDb(const std::string& email) {
    try {
        // 1. 开启事务
        odb::transaction trans(_db->begin());
        // 2. 查询用户信息：SELECT * FROM tbl_user WHERE email = ?
        auto result = std::unique_ptr<UserEntity>(
            _db->query_one<UserEntity>(odb::query<UserEntity>::email == email));
        // 3. 提交事务
        trans.commit();
        // 4. 返回结果
        if (result) {
            INF("User found in DB by email: email={}", email);
            return fromEntity(*result);
        }
        INF("User not found in DB by email: email={}", email);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get user from DB by email: email={}, error={}", email, e.what());
        return std::nullopt;
    }
}
// 性能取舍说明：复用完整查询而非 SELECT 1（ODB 无内置 count，View 投影成本 > 收益；
// 本表 6 列小表，唯一索引查询微秒级，存在性检查代价可忽略。表膨胀到 20+ 列大字段时
// 再引入 ODB View 做轻量投影）
bool UserData::existNicknameInDb(const std::string& nickname) {
    auto user = getUserByNicknameFromDb(nickname);
    return user.has_value();
}

bool UserData::existEmailInDb(const std::string& email) {
    auto user = getUserByEmailFromDb(email);
    return user.has_value();
}

/**
 * @brief 保存用户信息到 Redis 缓存（三键写入同一份 JSON）
 * @note 三键规则与 deleteUserCache/getUserFromCache* 严格对称
 * @return false 仅表示序列化失败或 Redis 异常（调用方应据场景决定是否阻断）
 */
bool UserData::saveUserToCache(const UserInfo& userInfo) {
    try {
        // 1. 生成缓存key（同一用户被三把钥匙索引：id/昵称/邮箱）
        std::string userIdKey = "user:userId:" + userInfo._userId;
        std::string nicknameKey = "user:nickname:" + userInfo._nickname;
        std::string emailKey = "user:email:" + userInfo._email;

        // 2. 序列化用户信息为JSON字符串（字段名与 fromCacheJson 解析处严格一致）
        Json::Value userJson;
        userJson["userId"] = userInfo._userId;
        userJson["nickname"] = userInfo._nickname;
        userJson["password"] = userInfo._password;
        userJson["email"] = userInfo._email;
        userJson["status"] = static_cast<int>(userInfo._status);

        // 3. JSON → 字符串；失败提前返回，绝不把空值写进缓存（"缓存了空气"的阴险 bug）
        auto jsonStr = biteutil::JSON::serialize(userJson);
        if (!jsonStr.has_value()) {
            WRN("Failed to serialize user info for cache");
            return false;
        }

        // 4. 锁内三键 setex（SET with EXpire：写值 + 1h TTL 一步完成）
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            _redis->setex(userIdKey, USER_CACHE_TTL_SECONDS, jsonStr.value());
            _redis->setex(nicknameKey, USER_CACHE_TTL_SECONDS, jsonStr.value());
            _redis->setex(emailKey, USER_CACHE_TTL_SECONDS, jsonStr.value());
        }

        INF("User saved to cache: userId={}", userInfo._userId);
        return true;
    } catch (const std::exception& e) {
        ERR("Failed to save user to cache: userId={}, error={}", userInfo._userId, e.what());
        return false;
    }
}

// ─────────────────────────────────────────────────────────────
// 缓存读取三兄弟 + 反序列化唯一映射点
// 设计全景（Cache-Aside 读路径）：
//   读用户信息时，缓存层先接招：
//     命中 → 反序列化返回（省一次 DB 往返）
//     未命中 → 返回 nullopt，由上层回源 DB 并调用 saveUserToCache 回填
//   本层永远不替上层决定"是否回源"，只报告"缓存里有没有/解析不解析得了"
// ─────────────────────────────────────────────────────────────

/**
 * @brief 缓存 JSON → UserInfo 的唯一反序列化映射点
 * @param jsonStr Redis 中存储的序列化用户 JSON（由 saveUserToCache 写入）
 * @return 成功返回 UserInfo；JSON 损坏/字段类型不符返回 nullopt
 * @note 与 fromEntity 对称：一个管 DB 实体→业务对象，一个管缓存 JSON→业务对象。
 *       将来 UserInfo 加字段，只改这两处，杜绝 6 处查询漏改的静默丢数据。
 * @note jsoncpp 行为：asString 对缺失字段返回 ""，asInt 对缺失字段返回 0（=Offline），
 *       对类型完全不匹配时抛异常 → 被调用方 try/catch 接住。缓存脏数据最多存活至 TTL 过期。
 */
std::optional<UserInfo> UserData::fromCacheJson(const std::string& jsonStr) {
    auto jsonOpt = biteutil::JSON::unserialize(jsonStr);   // 字符串 → Json::Value（含 UTF-8 校验）
    if (!jsonOpt.has_value()) {
        // 反序列化失败 = 缓存里有不可解析的脏数据：返回 nullopt，让上层走 DB 兜底
        return std::nullopt;
    }
    const Json::Value& json = jsonOpt.value();
    // 字段名必须与 saveUserToCache 写入的 key 严格一致（userId/nickname/password/email/status）
    UserInfo userInfo;
    userInfo._userId = json["userId"].asString();
    userInfo._nickname = json["nickname"].asString();
    userInfo._password = json["password"].asString();
    userInfo._email = json["email"].asString();
    userInfo._status = static_cast<UserStatus>(json["status"].asInt());
    return userInfo;
}

// 缓存读取五步模板（三个查询共用，仅 key 前缀与日志字段不同）：
//   Step 1 拼 key     → "user:userId:" / "user:nickname:" / "user:email:" + 身份值
//   Step 2 锁内 GET   → 锁只护 redis 连接（非线程安全），不护后续 CPU 活
//   Step 3 判断命中   → 未命中/空值 = 正常业务状态（TTL 过期、首次访问），INF 级别
//   Step 4 锁外解析   → 反序列化放锁外（可能耗时），交给 fromCacheJson 统一处理
//   Step 5 分级日志   → 命中 INF / 缓存损坏 WRN / Redis 异常 ERR，级别即诊断信号
std::optional<UserInfo> UserData::getUserFromCacheByUserId(const std::string& userId) {
    try {
        // Step 1: 生成缓存 key。三键规则与 saveUserToCache/deleteUserCache 严格对称，
        //         同一用户被 id/昵称/邮箱三把钥匙索引，写删都必须三键联动。
        std::string key = "user:userId:" + userId;

        // Step 2+3: 锁内取缓存值。redis++ 的 Redis 连接对象非线程安全（命令管道共享），
        //           RPC server 多线程并发访问时必须互斥；锁范围只包 GET 这一瞬。
        std::string jsonStr;
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                // 未命中是常态（首次访问 / TTL 1h 自然过期），不是错误 → INF 而非 WRN
                INF("User not found in cache by userId: userId={}", userId);
                return std::nullopt;   // 语义 = "缓存没有答案"，上层自行决定是否回源 DB
            }
            // 把值拷出锁外（string 拷贝几十字节，代价极小），锁一释放就立即开始解析
            jsonStr = result.value();
        }

        // Step 4+5: 锁外统一反序列化。命中且解析成功 → 返回；解析失败 → 脏数据告警 WRN。
        auto userInfo = fromCacheJson(jsonStr);
        if (userInfo.has_value()) {
            INF("User found in cache by userId: userId={}", userId);
            return userInfo;
        }
        WRN("Failed to unserialize user from cache: userId={}", userId);   // 缓存损坏属异常，值得 WRN
        return std::nullopt;
    } catch (const std::exception& e) {
        // Redis 连接异常（服务不可达等）：记 ERR 并返回 nullopt → 上层回源 DB，缓存故障不拖垮主链路
        ERR("Failed to get user from cache by userId: userId={}, error={}", userId, e.what());
        return std::nullopt;
    }
}

// 与 getUserFromCacheByUserId 完全同构（见其上"缓存读取五步模板"），仅 key 前缀与日志字段不同
std::optional<UserInfo> UserData::getUserFromCacheByNickname(const std::string& nickname) {
    try {
        std::string key = "user:nickname:" + nickname;
        std::string jsonStr;
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("User not found in cache by nickname: nickname={}", nickname);
                return std::nullopt;
            }
            jsonStr = result.value();
        }
        auto userInfo = fromCacheJson(jsonStr);
        if (userInfo.has_value()) {
            INF("User found in cache by nickname: nickname={}", nickname);
            return userInfo;
        }
        WRN("Failed to unserialize user from cache: nickname={}", nickname);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get user from cache by nickname: nickname={}, error={}", nickname, e.what());
        return std::nullopt;
    }
}

// 与 getUserFromCacheByUserId 完全同构（见其上"缓存读取五步模板"），仅 key 前缀与日志字段不同
std::optional<UserInfo> UserData::getUserFromCacheByEmail(const std::string& email) {
    try {
        std::string key = "user:email:" + email;
        std::string jsonStr;
        {
            std::lock_guard<std::mutex> lock(_redisMutex);
            auto result = _redis->get(key);
            if (!result.has_value() || result.value().empty()) {
                INF("User not found in cache by email: email={}", email);
                return std::nullopt;
            }
            jsonStr = result.value();
        }
        auto userInfo = fromCacheJson(jsonStr);
        if (userInfo.has_value()) {
            INF("User found in cache by email: email={}", email);
            return userInfo;
        }
        WRN("Failed to unserialize user from cache: email={}", email);
        return std::nullopt;
    } catch (const std::exception& e) {
        ERR("Failed to get user from cache by email: email={}, error={}", email, e.what());
        return std::nullopt;
    }
}

// ─────────────────────────────────────────────────────────────
// deleteUserCache：缓存失效（Cache-Aside "写"路径的收尾动作）
// 写路径完整流程：先写 DB（saveUserToDb）→ 后删缓存（本函数）
//   "管理员先把第二版书上架(写DB)，再从书桌上收走旧版书(删缓存)"
//   下次任何读请求都会因缓存缺失而强制回源 DB，自然拿到新数据
// ─────────────────────────────────────────────────────────────

/**
 * @brief 删除用户信息的全部三个缓存键
 * @param userId   用户ID   —— 拼 user:userId: 键
 * @param nickname 用户昵称 —— 拼 user:nickname: 键
 * @param email    用户邮箱 —— 拼 user:email: 键
 * @return void（刻意不返回成功与否，见下方"为何是 void"注释）
 * @note 三个参数必须与 saveUserToCache 写入时使用的身份信息一致；
 *       任一键残留都会让下一次读命中旧数据（脏缓存），故删必三键全删。
 */
void UserData::deleteUserCache(const std::string& userId, const std::string& nickname,
                               const std::string& email) {
    try {
        // Step 1: 拼三键 —— key 拼装规则只存在于本类内部（save/get/delete 三处对称），
        //         不泄露给上层；上层只需交出 id/昵称/邮箱三样"原料"
        std::string userIdKey = "user:userId:" + userId;
        std::string nicknameKey = "user:nickname:" + nickname;
        std::string emailKey = "user:email:" + email;

        // Step 2: 锁内执行三个 DEL —— redis 连接非线程安全，三键删除须整体持锁，
        //         保证并发读写不会出现"删了一半"的中间态被其他线程读到
        std::lock_guard<std::mutex> lock(_redisMutex);
        _redis->del(userIdKey);
        _redis->del(nicknameKey);
        _redis->del(emailKey);

        INF("User cache deleted: userId={}", userId);
    } catch (const std::exception& e) {
        // 为何是 void：删缓存失败不能阻断主流程（退出登录/改资料等）。
        // Cache-Aside 哲学：缓存删不掉，最多让旧数据多活到 TTL 自然过期(1h)，
        // 绝不能因为 Redis 抖动把"退出登录"这种主操作搞失败 → 只记 ERR，调用方照常继续
        ERR("Failed to delete user cache: userId={}, error={}", userId, e.what());
    }
}

} // namespace userService
