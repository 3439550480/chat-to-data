#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <thread>
#include <chrono>
#include <atomic>
#include "dbDriver/database.h"
#include "dbDriver/databaseFactory.h"
#include "dbDriver/databaseSchema.h"

namespace databaseService {

// 连接过期策略：超过 1 小时未使用判定为过期；清理线程每 5 分钟检查一次
constexpr int64_t HOUR_1 = 60 * 60 * 1000;
constexpr int64_t MINUTE_5 = 5 * 60 * 1000;
// 默认连接ID：专供智能 Excel 场景（不属于任何用户，不参与过期清理）
const std::string EXCEL_DEFAULT_CONN_ID = "excel_default";

// 连接信息
struct ConnInfo {
    std::string connectionId;         // 连接ID（默认连接为 excel_default，其余为 UUID）
    std::shared_ptr<IDatabase> db;    // 数据库连接实例
    std::string userId;               // 所属用户ID
    int64_t createTime;               // 创建时间（毫秒）
    int64_t lastActiveTime;           // 最近一次使用时间（毫秒）
    bool isDefaultConnection;         // 是否默认连接
    std::string sqliteFilePath;       // SQLite 本地文件路径（仅 SQLite 连接有效）
};

// 连接管理器：按连接ID索引连接，按用户ID索引连接集合，后台线程清理过期连接
class DBConnMgr {
public:
    DBConnMgr();
    ~DBConnMgr();
    // 创建数据库连接（接收已创建好的数据库实例）
    // @param sqliteFilePath SQLite 本地文件路径，SQLite 连接时传入，其他传空字符串
    // @return connectionId
    std::string createConnection(const std::string& userId,
                                 std::shared_ptr<IDatabase> db,
                                 bool isDefaultConnection,
                                 const std::string& sqliteFilePath = "");
    // 获取数据库连接（顺带刷新 lastActiveTime 并探活）
    std::shared_ptr<ConnInfo> getConnection(const std::string& connectionId);
    // 删除数据库连接（默认连接不删）
    bool removeConnection(const std::string& connectionId);
    // 删除用户所有连接，返回被删除的连接ID列表
    std::vector<std::string> deleteUserAllConnections(const std::string& userId);
    // 获取用户的所有连接ID列表
    std::vector<std::string> getUserConnectionIds(const std::string& userId);
    // 清理过期连接
    void cleanExpiredConnections();
private:
    // 当前时间戳（毫秒，steady_clock 单调时钟）
    int64_t getCurrentTimeMillis();
private:
    std::unordered_map<std::string, ConnInfo> _connections;                     // connId → 连接信息
    std::unordered_map<std::string, std::vector<std::string>> _userIdToConnIds; // userId → connIds
    std::shared_mutex _mutex;                       // 保护两张索引表
    std::thread _cleanThread;                       // 清理线程
    std::atomic<bool> _stopClean{false};            // 停止标志
    // 课件问题㊹修复：清理线程用条件变量等待，析构时立即唤醒退出
    //（课件用裸 sleep_for(5min)，析构 join 最长阻塞 5 分钟）
    std::mutex _cleanMutex;
    std::condition_variable _cleanCv;
};

} // namespace databaseService
