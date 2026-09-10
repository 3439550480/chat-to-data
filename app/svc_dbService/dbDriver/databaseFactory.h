#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include "database.h"
#include "databaseSchema.h"

namespace databaseService {

// 数据库工厂类：工厂模式 + 单例模式，负责创建不同类型的数据库实例
// 上层只持有 IDatabase 与 DBConfig，不感知具体驱动类名
class DatabaseFactory {
public:
    // 获取单例（Meyers 单例：函数内 static，C++11 起初始化线程安全——
    // 课件为双检锁 + static 成员 + recursive_mutex，此实现等价且更短）
    static DatabaseFactory& getInstance();

    // 防拷贝
    DatabaseFactory(const DatabaseFactory&) = delete;
    DatabaseFactory& operator=(const DatabaseFactory&) = delete;

    // 注册数据库类型（驱动动态注册入口；同类型重复注册拒绝）
    void registerDatabase(DBType type,
                          std::function<std::shared_ptr<IDatabase>(DBConfig*)> creator);
    // 创建数据库实例（空配置/未注册类型/非法配置三类失败返回 nullptr）
    std::shared_ptr<IDatabase> createDatabase(DBConfig* config);
    // 获取支持的数据库类型
    std::vector<DBType> getSupportedTypes();
    // 检查是否支持指定数据库类型
    bool isSupported(DBType type);
private:
    DatabaseFactory();   // 构造即注册 MySQL/SQLite 两个内置驱动
    // 数据库类型 → 创建器 的映射
    std::unordered_map<DBType, std::function<std::shared_ptr<IDatabase>(DBConfig*)>> _creators;
};

} // namespace databaseService
