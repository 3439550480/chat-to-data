#include <algorithm>
#include <chrono>
#include <bite_scaffold/log.h>
#include "dbConnMgr.h"
#include "../common/utils.h"

namespace databaseService {

DBConnMgr::DBConnMgr() {
    // 清理线程：可中断等待 MINUTE_5，被唤醒（析构）立即退出（课件问题㊹修复）
    _cleanThread = std::thread([this]() {
        std::unique_lock<std::mutex> lock(_cleanMutex);
        while (!_stopClean.load()) {
            // 等待 5 分钟或被 notify；返回 true 表示收到停止信号
            if (_cleanCv.wait_for(lock, std::chrono::milliseconds(MINUTE_5),
                                  [this] { return _stopClean.load(); })) {
                break;
            }
            // 释放 _cleanMutex 再干活（清理过程持的是 _mutex，两把锁不同层级）
            lock.unlock();
            cleanExpiredConnections();
            lock.lock();
        }
    });
    INF("DBConnMgr started, cleanup thread running");
}

DBConnMgr::~DBConnMgr() {
    // 1. 置停止标志并【立即唤醒】清理线程（㊹：不等待 5 分钟）
    {
        std::lock_guard<std::mutex> lock(_cleanMutex);
        _stopClean.store(true);
    }
    _cleanCv.notify_all();
    // 2. 等待线程退出
    if (_cleanThread.joinable()) {
        _cleanThread.join();
    }
    // 3. 清理所有连接（shared_ptr 析构触发 IDatabase 析构 → disconnect）
    {
        std::lock_guard<std::shared_mutex> lock(_mutex);
        _connections.clear();
        _userIdToConnIds.clear();
    }
    INF("DBConnMgr destroyed");
}

std::string DBConnMgr::createConnection(const std::string& userId,
                                        std::shared_ptr<IDatabase> db,
                                        bool isDefaultConnection,
                                        const std::string& sqliteFilePath) {
    // 1. 连接ID：默认连接固定 excel_default，其余用 UUID
    std::string connectionId;
    if (isDefaultConnection) {
        connectionId = EXCEL_DEFAULT_CONN_ID;
    } else {
        connectionId = chat2Data::Utils::generateUuid();
    }
    std::lock_guard<std::shared_mutex> lock(_mutex);
    // 2. 构造连接信息
    ConnInfo connInfo;
    connInfo.connectionId = connectionId;
    connInfo.db = db;
    connInfo.userId = userId;
    connInfo.createTime = getCurrentTimeMillis();
    connInfo.lastActiveTime = connInfo.createTime;
    connInfo.isDefaultConnection = isDefaultConnection;
    connInfo.sqliteFilePath = sqliteFilePath;
    // 3. 登记连接索引
    _connections[connectionId] = connInfo;
    // 4. 非默认连接才进用户索引（默认连接属于 Excel 场景，不归属任何用户）
    if (!isDefaultConnection && !userId.empty()) {
        _userIdToConnIds[userId].push_back(connectionId);
    }
    INF("Connection created: connId={}, userId={}, isDefault={}",
        connectionId, userId, isDefaultConnection);
    return connectionId;
}

// 获取连接：探活通过则刷新活跃时间并返回连接信息副本
std::shared_ptr<ConnInfo> DBConnMgr::getConnection(const std::string& connectionId) {
    // 课件问题㊸修复：此处要【写】lastActiveTime，必须用写锁（unique_lock）。
    // 课件用 shared_lock 却在锁内改字段——并发读时同时写同一变量，属数据竞争，
    // 最坏后果是"正在使用的连接活跃时间丢更新，被清理线程误杀"
    std::unique_lock<std::shared_mutex> lock(_mutex);
    auto it = _connections.find(connectionId);
    if (it != _connections.end()) {
        if (it->second.db && it->second.db->ping()) {
            it->second.lastActiveTime = getCurrentTimeMillis();
            return std::make_shared<ConnInfo>(it->second);
        }
    }
    return nullptr;
}

// 删除连接：默认连接不删；同时维护用户索引
bool DBConnMgr::removeConnection(const std::string& connectionId) {
    std::lock_guard<std::shared_mutex> lock(_mutex);
    auto it = _connections.find(connectionId);
    if (it == _connections.end()) {
        return false;
    }
    // 默认连接不可删除（调用方请求"成功"但实际保留，与课件语义一致）
    if (it->second.isDefaultConnection) {
        INF("Default connection is not removable: connId={}", connectionId);
        return true;
    }
    // 1. 断开数据库连接
    it->second.db->disconnect();
    // 2. 从用户索引中摘除
    auto userIt = _userIdToConnIds.find(it->second.userId);
    if (userIt != _userIdToConnIds.end()) {
        auto& connIds = userIt->second;
        connIds.erase(std::remove(connIds.begin(), connIds.end(), connectionId), connIds.end());
        if (connIds.empty()) {
            _userIdToConnIds.erase(userIt);
        }
    }
    // 3. 从连接索引中删除
    _connections.erase(it);
    INF("Connection removed: connId={}", connectionId);
    return true;
}

// 删除用户所有连接
std::vector<std::string> DBConnMgr::deleteUserAllConnections(const std::string& userId) {
    std::lock_guard<std::shared_mutex> lock(_mutex);
    auto userIt = _userIdToConnIds.find(userId);
    if (userIt == _userIdToConnIds.end()) {
        return {};
    }
    std::vector<std::string> deletedConnIds;
    // 逐个断开并删除
    for (const auto& connId : userIt->second) {
        auto connIt = _connections.find(connId);
        if (connIt != _connections.end()) {
            connIt->second.db->disconnect();
            _connections.erase(connIt);
            deletedConnIds.push_back(connId);
            INF("User connection deleted: connId={}", connId);
        }
    }
    _userIdToConnIds.erase(userIt);
    INF("User all connections deleted: userId={}, count={}", userId, deletedConnIds.size());
    return deletedConnIds;
}

// 获取用户的所有连接ID列表
std::vector<std::string> DBConnMgr::getUserConnectionIds(const std::string& userId) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto userIt = _userIdToConnIds.find(userId);
    if (userIt == _userIdToConnIds.end()) {
        return {};
    }
    return userIt->second;   // 返回副本，调用方无需持锁
}

// 清理过期连接（默认连接永不清理）
void DBConnMgr::cleanExpiredConnections() {
    std::lock_guard<std::shared_mutex> lock(_mutex);
    int64_t now = getCurrentTimeMillis();
    std::vector<std::string> expiredConnIds;
    // 1. 收集过期连接
    for (const auto& pair : _connections) {
        if (pair.second.isDefaultConnection) {
            continue;
        }
        if (now - pair.second.lastActiveTime > HOUR_1) {
            expiredConnIds.push_back(pair.first);
        }
    }
    // 2. 逐个断开并从索引中摘除
    for (const auto& connId : expiredConnIds) {
        auto it = _connections.find(connId);
        if (it == _connections.end()) {
            continue;
        }
        auto userIt = _userIdToConnIds.find(it->second.userId);
        if (userIt != _userIdToConnIds.end()) {
            auto& connIds = userIt->second;
            connIds.erase(std::remove(connIds.begin(), connIds.end(), connId), connIds.end());
            if (connIds.empty()) {
                _userIdToConnIds.erase(userIt);
            }
        }
        it->second.db->disconnect();
        _connections.erase(it);
        INF("Expired connection cleaned: connId={}", connId);
    }
}

// 当前时间戳（毫秒）
int64_t DBConnMgr::getCurrentTimeMillis() {
    // steady_clock 单调时钟：不受系统时间调整影响，适合计算"间隔"
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

} // namespace databaseService
