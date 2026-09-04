#pragma once

#include <exception>
#include <string>

namespace chat2Data {

// 明确大小：保证底层存储类型为 int（通常 4 字节），跨平台/序列化时行为确定。
// 默认情况：不指定时，编译器自行选择能容纳所有枚举值的最小整型，可能导致不同平台大小不一致。
// 其他可选类型：uint8_t, int16_t, uint32_t 等，根据错误码数量选择合适大小以节省内存。
// 💡 这里选 int 是因为错误码最大到 699，int 绰绰有余，且与大多数 API 的返回值类型兼容。
enum class ErrorCode : int {
    // ==================== 通用成功码 ====================
    SUCCESS = 0,

    // ==================== 用户服务错误码 (100-199) ====================
    // 用户子服务错误码范围 100~199
    // USER_xxx_XXX
    USER_PASSWORD_ENCRYPT_ERROR = 100,
    USER_PASSWORD_INVALID,
    USER_SESSION_CREATE_ERROR,
    USER_SESSION_CACHE_ERROR,
    USER_SESSION_DELETE_ERROR,
    USER_SESSION_INVALID,
    USER_SAVE_VERIFY_CODE_FAILED,
    USER_VERIFY_CODE_EXPIRED,
    USER_VERIFY_CODE_ERROR,
    USER_LOGIN_FAILED,
    USER_SESSION_LOGIN_FAILED,
    USER_EMAIL_LOGIN_PARAM_ERROR,
    USER_LOGOUT_FAILED,
    USER_SAVE_USER_INFO_FAILED,
    USER_NOT_FOUND,
    USER_NICKNAME_EMPTY,
    USER_NICKNAME_EXIST,
    USER_EMAIL_EMPTY,
    USER_EMAIL_EXIST,
    USER_REGISTER_PARAM_ERROR,
    USER_GET_USER_INFO_PARAM_ERROR,

    // ==================== 文件服务错误码 (200-299) ====================
    // 文件子服务错误码范围 200~299
    // FILE_xxx_xxx
    FILE_PARAM_INVALID = 200,
    FILE_NOT_FOUND,
    FILE_UPLOAD_FASTDFS_FAILED,
    FILE_SAVE_LOCAL_FAILED,
    FILE_DELETE_FAILED,
    FILE_INFO_SAVE_FAILED,
    FILE_EXCEL_PARSE_FAILED,
    FILE_SQLITE_UPLOAD_FAILED,
    FILE_PERMISSION_DENIED,
    FILE_WORKSHEET_SAVE_FAILED,
    FILE_UPLOAD_EXCEED_MAX_SIZE,
    FILE_SESSION_INVALID,
    FILE_FILEID_EMPTY,
    FILE_SQLITE_FILE_NOT_FOUND,
    FILE_EXCEL_WORKSHEETS_GET_FAILED,
    FILE_STORAGE_SERVICE_FAILED,

    // ==================== 数据库服务错误码 (300-399) ====================
    // 数据库子服务错误码范围 300~399
    // DB_xxx_xxx
    DB_PARAM_INVALID = 300,
    DB_CONNECTION_FAILED,
    DB_CONNECTION_NOT_EXISTS,
    DB_CONNECTION_INVALID,
    DB_SQL_EXECUTE_FAILED,
    DB_TABLE_NOT_FOUND,
    DB_TEMP_TABLE_CREATE_FAILED,
    DB_BACKUP_TABLE_FAILED,
    DB_SQLITE_DOWNLOAD_FAILED,
    DB_TEMP_TABLE_NOT_FOUND,
    DB_TEMP_TABLE_DELETE_FAILED,
    DB_SAMPLE_DATA_GET_FAILED,
    DB_TABLE_DATA_GET_FAILED,
    DB_TABLE_STRUCT_GET_FAILED,
    DB_IMPORT_DATA_FAILED,
    DB_DROP_TABLE_FAILED,
    DB_USER_ALL_CONN_DELETE_FAILED,

    // ==================== Excel解析服务错误码 (400-499) ====================
    // Excel解析子服务错误码范围 400~499
    // EXCEL_xxx_xxx
    EXCEL_FILE_PATH_INVALID = 400,
    EXCEL_FILE_OPEN_FAILED,
    EXCEL_WORKSHEET_NOT_FOUND,
    EXCEL_PARSE_FAILED,
    EXCEL_DATA_INVALID,

    // ==================== 通知服务错误码 (500-599) ====================
    // 通知子服务错误码范围 500~599
    // NOTIFY_xxx_xxx
    NOTIFY_PARAM_EMPTY = 500,
    NOTIFY_SEND_FAILED,
    NOTIFY_EMAIL_INVALID,
    NOTIFY_CODE_INVALID,
    NOTIFY_SUBJECT_INVALID,

    // ==================== AI服务错误码 (600-699) ====================
    // AI子服务错误码范围 600~699
    // AI_xxx_xxx
    AI_PARAM_INVALID = 600,
    AI_SESSION_CREATE_ERROR,
    AI_SESSION_SAVE_ERROR,
    AI_SESSION_NOT_FOUND,
    AI_SESSION_PERMISSION_DENIED,
    AI_GET_MODELS_FAILED,
    AI_SEND_MESSAGE_FAILED,
    AI_SEND_MESSAGE_NOT_IMPLEMENTED,
    AI_MODEL_NOT_AVAILABLE,
    AI_UPDATE_SESSION_FILE_FAILED,
    AI_CHATSESSION_NOT_OWNED_BY_USER
};

// 将错误码转换为错误描述符
std::string error2String(ErrorCode code);

// 根据错误码获取所属的服务名称
std::string getServiceName(ErrorCode code);

// 程序异常类：遇到非法情况统一抛出，携带错误码与错误描述.
//继承
//一个将"机器可读的错误码"和"人可读的错误描述"打包在一起的传输载体
class Chat2DataException : public std::exception {
public:
    Chat2DataException(ErrorCode code);

    // 获取异常信息
    virtual const char* what() const noexcept override;

    // 获取错误码
    ErrorCode getErrorCode() const;

private:
    ErrorCode _errorCode;
    std::string _errorMessage;  // 构造时就拼好完整消息 "[Service]: message"（每实例独立，构造后不可变）
};

} // end chat2Data
