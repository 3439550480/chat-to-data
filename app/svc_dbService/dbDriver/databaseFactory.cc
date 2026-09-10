#include <bite_scaffold/log.h>
#include "databaseFactory.h"
#include "mysqlDatabase.h"
#include "sqliteDatabase.h"

namespace databaseService {

// 类型名（日志用）
static const char* dbTypeName(DBType type) {
    return (type == DBType::MYSQL) ? "MySQL" : "SQLite";
}

DatabaseFactory::DatabaseFactory() {
    // 注册 MySQL 驱动（dynamic_cast 兜配置类型错配）
    registerDatabase(DBType::MYSQL, [](DBConfig* config) -> std::shared_ptr<IDatabase> {
        MySQLConfig* mysqlConfig = dynamic_cast<MySQLConfig*>(config);
        if (mysqlConfig == nullptr) {
            return std::shared_ptr<IDatabase>(nullptr);
        }
        return std::make_shared<MySQLDatabase>(*mysqlConfig);
    });
    // 注册 SQLite 驱动
    registerDatabase(DBType::SQLITE, [](DBConfig* config) -> std::shared_ptr<IDatabase> {
        SQLiteConfig* sqliteConfig = dynamic_cast<SQLiteConfig*>(config);
        if (sqliteConfig == nullptr) {
            return std::shared_ptr<IDatabase>(nullptr);
        }
        return std::make_shared<SQLiteDatabase>(*sqliteConfig);
    });
}

DatabaseFactory& DatabaseFactory::getInstance() {
    // Meyers 单例：首次调用构造（线程安全由语言保证），此后零开销
    static DatabaseFactory instance;
    return instance;
}

void DatabaseFactory::registerDatabase(
    DBType type, std::function<std::shared_ptr<IDatabase>(DBConfig*)> creator) {
    // 1. 检查数据库类型是否已注册
    if (_creators.find(type) != _creators.end()) {
        ERR("DatabaseFactory: database type already registered: {}", dbTypeName(type));
        return;
    }
    // 2. 注册数据库类型
    _creators[type] = creator;
    INF("Registered database driver: {}", dbTypeName(type));
}

std::shared_ptr<IDatabase> DatabaseFactory::createDatabase(DBConfig* config) {
    // 1. 检查配置是否为空
    if (config == nullptr) {
        ERR("DatabaseFactory: config is nullptr");
        return nullptr;
    }
    // 2. 获取数据库的创建器
    DBType type = config->getType();
    auto it = _creators.find(type);
    if (it == _creators.end()) {
        ERR("DatabaseFactory: unsupported database type: {}", dbTypeName(type));
        return nullptr;
    }
    // 3. 检查配置是否有效（如 SQLite 文件是否存在、MySQL 必填项是否完整）
    if (!config->validConfig()) {
        ERR("DatabaseFactory: invalid config for {}", dbTypeName(type));
        return nullptr;
    }
    // 4. 创建数据库实例
    auto database = it->second(config);
    INF("DatabaseFactory: created {} database instance", dbTypeName(type));
    return database;
}

std::vector<DBType> DatabaseFactory::getSupportedTypes() {
    std::vector<DBType> types;
    types.reserve(_creators.size());
    for (const auto& pair : _creators) {
        types.push_back(pair.first);
    }
    return types;
}

bool DatabaseFactory::isSupported(DBType type) {
    return _creators.find(type) != _creators.end();
}

} // namespace databaseService
