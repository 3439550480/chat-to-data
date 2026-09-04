#include "errorHandler.h"
#include <unordered_map>

namespace chat2Data {

std::string error2String(ErrorCode code) {
    static std::unordered_map<ErrorCode, std::string> errorMap = {
        // ==================== 通用成功码 ====================
        {ErrorCode::SUCCESS, "操作成功"},

        // ==================== 用户服务错误码 ====================
        // 用户子服务错误码描述符
        {ErrorCode::USER_PASSWORD_ENCRYPT_ERROR, "密码加密失败"},
        {ErrorCode::USER_PASSWORD_INVALID, "密码无效"},
        {ErrorCode::USER_SESSION_CREATE_ERROR, "会话创建失败"},
        {ErrorCode::USER_SESSION_CACHE_ERROR, "会话缓存保存失败"},
        {ErrorCode::USER_SESSION_DELETE_ERROR, "会话删除失败"},
        {ErrorCode::USER_SESSION_INVALID, "会话无效或已过期"},
        {ErrorCode::USER_SAVE_VERIFY_CODE_FAILED, "保存验证码失败"},
        {ErrorCode::USER_VERIFY_CODE_EXPIRED, "验证码已过期"},
        {ErrorCode::USER_VERIFY_CODE_ERROR, "验证码错误"},
        {ErrorCode::USER_LOGIN_FAILED, "登录失败"},
        {ErrorCode::USER_SESSION_LOGIN_FAILED, "会话登录失败"},
        {ErrorCode::USER_EMAIL_LOGIN_PARAM_ERROR, "邮箱登录参数错误，邮箱、验证码、验证码id为空"},
        {ErrorCode::USER_LOGOUT_FAILED, "退出登录失败"},
        {ErrorCode::USER_SAVE_USER_INFO_FAILED, "保存用户信息失败"},
        {ErrorCode::USER_NOT_FOUND, "用户不存在"},
        {ErrorCode::USER_NICKNAME_EMPTY, "昵称不能为空"},
        {ErrorCode::USER_NICKNAME_EXIST, "昵称已存在"},
        {ErrorCode::USER_EMAIL_EMPTY, "邮箱不能为空"},
        {ErrorCode::USER_EMAIL_EXIST, "邮箱已存在"},
        {ErrorCode::USER_REGISTER_PARAM_ERROR,
         "注册参数错误，昵称或邮箱不能为空或已存在，密码长度必须大于等于6位，且不能包含空格"},
        {ErrorCode::USER_GET_USER_INFO_PARAM_ERROR, "获取用户信息参数错误，用户Id不能为空或已过期"},

        // ==================== 文件服务错误码 ====================
        // 文件子服务错误码描述符
        {ErrorCode::FILE_PARAM_INVALID, "参数无效"},
        {ErrorCode::FILE_NOT_FOUND, "文件不存在"},
        {ErrorCode::FILE_UPLOAD_FASTDFS_FAILED, "文件上传FastDFS失败"},
        {ErrorCode::FILE_SAVE_LOCAL_FAILED, "文件保存本地失败"},
        {ErrorCode::FILE_DELETE_FAILED, "文件删除失败"},
        {ErrorCode::FILE_INFO_SAVE_FAILED, "文件信息保存失败"},
        {ErrorCode::FILE_EXCEL_PARSE_FAILED, "Excel文件解析失败"},
        {ErrorCode::FILE_SQLITE_UPLOAD_FAILED, "SQLite文件上传失败"},
        {ErrorCode::FILE_PERMISSION_DENIED, "文件权限被拒绝"},
        {ErrorCode::FILE_WORKSHEET_SAVE_FAILED, "Worksheet信息保存失败"},
        {ErrorCode::FILE_UPLOAD_EXCEED_MAX_SIZE, "文件大小超过限制"},
        {ErrorCode::FILE_SESSION_INVALID, "会话无效或已过期"},
        {ErrorCode::FILE_FILEID_EMPTY, "文件ID为空"},
        {ErrorCode::FILE_SQLITE_FILE_NOT_FOUND, "SQLite文件不存在"},
        {ErrorCode::FILE_EXCEL_WORKSHEETS_GET_FAILED, "获取Excel工作表列表失败"},
        {ErrorCode::FILE_STORAGE_SERVICE_FAILED, "存储子服务调用失败"},

        // ==================== 数据库服务错误码 ====================
        // 数据库子服务错误码描述符
        {ErrorCode::DB_PARAM_INVALID, "参数无效"},
        {ErrorCode::DB_CONNECTION_FAILED, "数据库连接失败"},
        {ErrorCode::DB_CONNECTION_NOT_EXISTS, "数据库连接不存在"},
        {ErrorCode::DB_CONNECTION_INVALID, "数据库连接无效"},
        {ErrorCode::DB_SQL_EXECUTE_FAILED, "SQL语句执行失败"},
        {ErrorCode::DB_TABLE_NOT_FOUND, "表不存在"},
        {ErrorCode::DB_TEMP_TABLE_CREATE_FAILED, "临时表创建失败"},
        {ErrorCode::DB_BACKUP_TABLE_FAILED, "备份表失败"},
        {ErrorCode::DB_SQLITE_DOWNLOAD_FAILED, "SQLite文件下载失败"},
        {ErrorCode::DB_TEMP_TABLE_NOT_FOUND, "临时表不存在"},
        {ErrorCode::DB_TEMP_TABLE_DELETE_FAILED, "临时表删除失败"},
        {ErrorCode::DB_SAMPLE_DATA_GET_FAILED, "获取采样数据失败"},
        {ErrorCode::DB_TABLE_DATA_GET_FAILED, "获取表数据失败"},
        {ErrorCode::DB_TABLE_STRUCT_GET_FAILED, "获取表结构失败"},
        {ErrorCode::DB_IMPORT_DATA_FAILED, "导入数据失败"},
        {ErrorCode::DB_DROP_TABLE_FAILED, "删除表失败"},
        {ErrorCode::DB_USER_ALL_CONN_DELETE_FAILED, "删除用户所有连接失败"},

        // ==================== Excel解析服务错误码 ====================
        // Excel解析子服务错误码描述符
        {ErrorCode::EXCEL_FILE_PATH_INVALID, "文件路径无效"},
        {ErrorCode::EXCEL_FILE_OPEN_FAILED, "文件打开失败"},
        {ErrorCode::EXCEL_WORKSHEET_NOT_FOUND, "工作表不存在"},
        {ErrorCode::EXCEL_PARSE_FAILED, "Excel解析失败"},
        {ErrorCode::EXCEL_DATA_INVALID, "Excel数据无效"},

        // ==================== 通知服务错误码 ====================
        // 通知子服务错误码描述符
        {ErrorCode::NOTIFY_PARAM_EMPTY, "参数不能为空"},
        {ErrorCode::NOTIFY_SEND_FAILED, "邮件发送失败"},
        {ErrorCode::NOTIFY_EMAIL_INVALID, "邮箱地址无效"},
        {ErrorCode::NOTIFY_CODE_INVALID, "验证码无效"},
        {ErrorCode::NOTIFY_SUBJECT_INVALID, "邮件主题无效"},

        // ==================== AI服务错误码 ====================
        // AI子服务错误码描述符
        {ErrorCode::AI_PARAM_INVALID, "参数无效"},
        {ErrorCode::AI_SESSION_CREATE_ERROR, "会话创建失败"},
        {ErrorCode::AI_SESSION_SAVE_ERROR, "会话保存失败"},
        {ErrorCode::AI_SESSION_NOT_FOUND, "会话不存在"},
        {ErrorCode::AI_SESSION_PERMISSION_DENIED, "会话权限被拒绝"},
        {ErrorCode::AI_GET_MODELS_FAILED, "获取模型列表失败"},
        {ErrorCode::AI_SEND_MESSAGE_FAILED, "发送消息失败"},
        {ErrorCode::AI_SEND_MESSAGE_NOT_IMPLEMENTED, "发送消息功能未实现"},
        {ErrorCode::AI_MODEL_NOT_AVAILABLE, "模型不可用"},
        {ErrorCode::AI_UPDATE_SESSION_FILE_FAILED, "更新Excel文件和聊天会话关联失败"},
        {ErrorCode::AI_CHATSESSION_NOT_OWNED_BY_USER, "聊天会话不属于当前用户"}
    };

    auto it = errorMap.find(code);//看code是否是我们定义的错误码
    if(it != errorMap.end())//如果找到了，就返回对应的错误描述
    {
        return it->second;
    }
    return "未知错误";//如果没找到，就返回未知错误
    // auto it = errorMap.find(code);
    // if (it != errorMap.end()) {
    //     return it->second;
    // }
    // return "未知错误";
}

std::string getServiceName(ErrorCode code) {
    //static_cast<目标类型>(表达式)   类型转换
    int codeValue = static_cast<int>(code);//将枚举值转换为整数

    if (codeValue == 0)//0表示操作成功，不属于任何子服务
    {
        return "请求操作成功";
    }

    //按错误码区间判断所属服务：每个子服务占用100个号段
    if (codeValue >= 100 && codeValue <= 199)
    {
        return "userService";
    }
    else if (codeValue >= 200 && codeValue <= 299)
    {
        return "fileService";
    }
    else if (codeValue >= 300 && codeValue <= 399)
    {
        return "dbService";
    }
    else if (codeValue >= 400 && codeValue <= 499)
    {
        return "excelService";
    }
    else if (codeValue >= 500 && codeValue <= 599)
    {
        return "notifyService";
    }
    else if (codeValue >= 600 && codeValue <= 699)
    {
        return "aiService";
    }
    else if (codeValue >= 700 && codeValue <= 799)
    {
        return "gatewayService";
    }

    return "未知服务";//不在任何已知区间内
}

// ==================== Chat2DataException ====================

Chat2DataException::Chat2DataException(ErrorCode code)
    : _errorCode(code)
    , _errorMessage("[" + getServiceName(code) + "]: " + error2String(code))//构造时就一次拼好完整消息
{
    //课件原方案：构造函数只存错误码，完整消息延迟到 what() 里拼接
    //_errorMessage = error2String(code);
}

// | 维度 | 原方案 (static) | 新方案 (成员变量) |
// | :--- | :--- | :--- |
// | 信息写入时机 | 每次调用 `what()` 时动态写入 | 构造时一次性写入 |
// | 信息读取方式 | 读共享可变状态 | 读对象私有不可变状态 |
// | 幂等性 | ❌ 不保证 | ✅ 严格保证 |
// | 隔离性 | ❌ 全局共享，互相污染 | ✅ 对象独立，互不干扰 |
// | 并发安全性 | ❌ Data Race | ✅ 天然安全 |
// | 设计范式 | 过程式思维（函数持有状态） | 面向对象思维（对象持有状态） |



const char* Chat2DataException::what() const noexcept {
    return _errorMessage.c_str();//每个实例独立、线程安全、零开销
    //课件原方案（有 bug，注释保留作对照）：
    //static std::string fullMessage;
    //fullMessage = "[" + getServiceName(_errorCode) + "]: " + _errorMessage;
    //return fullMessage.c_str();
    //缺陷1：static 局部变量被所有实例共享，e1.what() 返回的指针在 e2.what() 调用后内容被覆盖（多实例覆盖）
    //缺陷2：多线程同时调用 what() 会并发写同一个 static string，数据竞争，未定义行为
    //在存储到输出错误信息这个过程中，原错误信息可能被覆盖，导致 what() 返回的指针指向的内容不再是原来的错误信息，而是被覆盖后的新错误信息。
}

ErrorCode Chat2DataException::getErrorCode() const {
    return _errorCode;//返回构造时保存的错误码
}

} // end chat2Data
