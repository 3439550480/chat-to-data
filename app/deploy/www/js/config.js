// 应用配置
window.CONFIG = {
    // API基础URL（部署态：空串 = 同源相对路径，走网关 8000 端口映射；
    // api.js/auth.js 对空串有专门分支，拼相对路径请求）
    API_BASE_URL: '',
    
    // 文件服务API路径
    FILE_SERVER: {
        HEALTH: '/health',
        UPLOAD_FILE_INFO: '/api/file/upload/info',
        GET_FILE_INFO: '/api/file/info',
        UPLOAD_FILE: '/api/file/upload',
        DOWNLOAD_FILE: '/api/file/download',
        DELETE_FILE: '/api/file/',
        FILE_LISTS: '/api/file/list',
        PREVIEW_EXCEL: '/api/file/preview',
        HANDLE_FILE_CHAT_SESSION_MAP: '/api/file/chat/map',
        UPLOAD_SQLITE_FILE: '/api/file/sqlite/upload'
    },
    
    // 模型服务API路径
    MODEL_SERVICE: {
        // 获取会话列表（新规范：POST）
        GET_SESSIONS: '/api/ai/chatSessionLists',
        // 获取模型列表（新规范：POST）
        GET_MODELS: '/api/ai/models',
        // 创建新会话（POST）
        CREATE_SESSION: '/api/ai/session/create',
        // 发送消息（流式，新规范：POST）
        SEND_MESSAGE: '/api/ai/sendStreamMessage',
        // 删除会话（新规范：POST）
        DELETE_SESSION: '/api/ai/delete',
        // 获取会话历史（新规范：POST）
        GET_SESSION_HISTORY: '/api/ai/history'
    },
    
    // 用户服务API路径
    USER_SERVICE: {
        VALID_NICKNAME: '/api/user/valid/nickname',
        VALID_EMAIL: '/api/user/valid/email',
        USER_REGISTER: '/api/user/register',
        SESSION_LOGIN: '/api/user/session/login',
        PASSWD_LOGIN: '/api/user/passwd/login',
        GET_CODE: '/api/user/code',
        VCODE_LOGIN: '/api/user/vcode/login',
        LOGOUT: '/api/user/logout',
        GET_USER_INFO: '/api/user/info'
    },
    
    // 默认配置
    DEFAULTS: {
        MODEL: 'glm-5.3-flash',
        REQUEST_ID_PREFIX: 'req_',
        SESSION_ID_PREFIX: 'sess_',
        USER_ID_PREFIX: 'user_'
    },
    
    // 文件上传配置
    UPLOAD: {
        MAX_FILE_SIZE: 10 * 1024 * 1024, // 10MB
        ALLOWED_EXTENSIONS: ['.xlsx'],
        CHUNK_SIZE: 1024 * 1024 // 1MB
    },
    
    // 界面配置
    UI: {
        MESSAGE_DISPLAY_DELAY: 100, // 消息显示延迟(ms)
        STREAM_UPDATE_INTERVAL: 50, // 流式更新间隔(ms)
        LOADING_TIMEOUT: 30000 // 加载超时时间(ms)
    }
};

// 全局状态管理
window.APP_STATE = {
    // 当前会话信息
    currentSession: {
        id: null,
        model: null,
        createdAt: null,
        messages: []
    },
    
    // 用户信息
    user: {
        id: 'default_user',
        requestId: null
    },
    
    // 文件信息
    currentFile: {
        id: null,
        name: null,
        size: null,
        data: null
    },
    
    // 模型列表
    models: [],
    
    // 文件列表
    files: [],
    
    // 会话列表
    sessions: [],
    
    // 界面状态
    ui: {
        currentPage: 'chat',
        currentTab: 'file-preview',
        isUploading: false,
        isSending: false,
        isConnected: false
    }
};

// 生成唯一ID
function generateId(prefix = '') {
    return prefix + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
}

// 生成请求ID
function generateRequestId() {
    return generateId(CONFIG.DEFAULTS.REQUEST_ID_PREFIX);
}

// 生成会话ID
function generateSessionId() {
    return generateId(CONFIG.DEFAULTS.SESSION_ID_PREFIX);
}

// 生成用户ID
function generateUserId() {
    return generateId(CONFIG.DEFAULTS.USER_ID_PREFIX);
}