#pragma once

#include <memory>
#include <string>
#include <optional>
#include <mutex>
#include <odb/mysql/database.hxx>
#include <sw/redis++/redis.h>
#include "../svc_userService/common.h"

// 前向声明：fromEntity 仅以引用方式使用 UserEntity，无需在此包含完整定义（避免引入 ODB 生成头）
class UserEntity;

namespace userService {

// 用户表数据操作类：封装对 tbl_user 的数据库(Cache-Aside 之 DB)与缓存(Redis) 双向存取
class UserData {
public:
    // 构造函数，注入外部创建的 MySQL 与 Redis 连接
    // 注：课件测试版曾用 (MysqlConfig, RedisConfig) 直接构造，与头文件声明矛盾(问题⑧)；
    //     采用依赖注入方案：连接由装配层用 biteodb::DBFactory/biteredis::RedisFactory 创建后注入
    UserData(std::shared_ptr<odb::database> db,
             std::shared_ptr<sw::redis::Redis> redis);

    // 保存用户信息到MySQL，如果用户存在则更新，不存在则插入
    bool saveUserToDb(const UserInfo& userInfo);

    // 从MySQL通过用户ID获取用户信息
    std::optional<UserInfo> getUserByUserIdFromDb(const std::string& userId);

    // 从MySQL通过用户昵称获取用户信息
    std::optional<UserInfo> getUserByNicknameFromDb(const std::string& nickname);

    // 从MySQL通过用户邮箱获取用户信息
    std::optional<UserInfo> getUserByEmailFromDb(const std::string& email);

    /**
     * @brief  从MySQL检测昵称是否存在
     * @param  nickname  用户昵称
     * @return 存在返回true，不存在返回false
     */
    bool existNicknameInDb(const std::string& nickname);

    // 从MySQL检测邮箱是否存在
    bool existEmailInDb(const std::string& email);

    // 保存用户信息到Redis缓存
    bool saveUserToCache(const UserInfo& userInfo);

    // 从Redis缓存通过用户ID获取用户信息
    std::optional<UserInfo> getUserFromCacheByUserId(const std::string& userId);

    // 从Redis缓存通过用户昵称获取用户信息
    std::optional<UserInfo> getUserFromCacheByNickname(const std::string& nickname);

    // 从Redis缓存通过用户邮箱获取用户信息
    std::optional<UserInfo> getUserFromCacheByEmail(const std::string& email);

    // 从Redis缓存删除用户信息
    void deleteUserCache(const std::string& userId, const std::string& nickname,
                         const std::string& email);

private:
    // 将 ODB 实体转换为业务结构体：实体与业务双模型的唯一映射点
    // 新增加字段时只改这里，杜绝 N 处查询漏改导致的数据静默丢失
    static UserInfo fromEntity(const UserEntity& entity);

    // 将缓存 JSON 字符串反序列化为业务结构体：缓存反序列化的唯一映射点
    // 与 fromEntity 对称：DB 侧实体转换 / 缓存侧 JSON 转换
    static std::optional<UserInfo> fromCacheJson(const std::string& jsonStr);

    std::shared_ptr<odb::database> _db;                 // MySQL数据库连接
    std::shared_ptr<sw::redis::Redis> _redis;           // Redis数据库连接
    std::mutex _redisMutex;                             // Redis操作互斥锁
    const static int USER_CACHE_TTL_SECONDS = 60 * 60;  // 1h = 60min * 60s
};

} // namespace userService
