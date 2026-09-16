// 主应用入口文件

/**
 * 清理JSON字符串中的控制字符，修复JSON解析错误
 * 问题：JSON字符串中可能包含未转义的控制字符（如换行符、制表符等），导致JSON.parse()失败
 * 解决：智能识别字符串值部分，只在字符串值内部转义控制字符
 * 定义为全局函数，供safeJSONParse使用
 * @param {string} jsonStr - 原始JSON字符串
 * @returns {string} 清理后的JSON字符串
 */
window.sanitizeJSONString = function(jsonStr) {
    if (!jsonStr || typeof jsonStr !== 'string') {
        return jsonStr;
    }
    
    let result = '';
    let i = 0;
    let inString = false; // 是否在字符串值内部
    
    while (i < jsonStr.length) {
        const char = jsonStr[i];
        const charCode = jsonStr.charCodeAt(i);
        
        // 处理反斜杠（转义字符）
        if (char === '\\') {
            if (i + 1 < jsonStr.length) {
                const nextChar = jsonStr[i + 1];
                // 检查是否是有效的转义序列
                if (nextChar === 'n' || nextChar === 'r' || nextChar === 't' || 
                    nextChar === '"' || nextChar === '\\' || nextChar === '/' ||
                    nextChar === 'b' || nextChar === 'f') {
                    // 标准转义序列，保留
                    result += char + nextChar;
                    i += 2;
                    continue;
                } else if (nextChar === 'u' && i + 5 < jsonStr.length) {
                    // \uXXXX转义序列
                    const hexChars = jsonStr.substring(i + 2, i + 6);
                    if (/^[0-9A-Fa-f]{4}$/.test(hexChars)) {
                        // 有效的十六进制编码
                        result += char + nextChar + hexChars;
                        i += 6;
                        continue;
                    }
                }
            }
            // 无效的转义序列或字符串末尾的反斜杠，转义它
            if (inString) {
                result += '\\\\';
            } else {
                result += '\\\\';
            }
            i++;
            continue;
        }
        
        // 处理引号（字符串边界）
        if (char === '"') {
            // 检查前面是否有奇数个连续的反斜杠（奇数个表示引号被转义）
            let backslashCount = 0;
            let j = i - 1;
            while (j >= 0 && jsonStr[j] === '\\') {
                backslashCount++;
                j--;
            }
            
            // 偶数个或0个反斜杠，这是真正的引号（字符串边界）
            if (backslashCount % 2 === 0) {
                inString = !inString;
            }
            result += char;
            i++;
            continue;
        }
        
        // 在字符串值内部，需要转义所有未转义的控制字符
        if (inString) {
            // 控制字符（\x00-\x1F），需要转义为\uXXXX格式
            // 注意：这里不需要检查是否已经转义，因为如果已经转义，前面会处理反斜杠
            if (charCode >= 0x00 && charCode <= 0x1F) {
                const hexCode = charCode.toString(16).padStart(4, '0').toUpperCase();
                result += '\\u' + hexCode;
            } else {
                // 保留其他字符
                result += char;
            }
        } else {
            // 在字符串值外部，移除控制字符（保留空格、换行等用于格式化）
            if (charCode === 0x09 || charCode === 0x0A || charCode === 0x0D || charCode === 0x20) {
                // 保留制表符、换行符、回车符和空格
                result += char;
            } else if (charCode >= 0x00 && charCode <= 0x1F) {
                // 移除其他控制字符
                // 不添加到result中
            } else {
                // 保留其他字符
                result += char;
            }
        }
        
        i++;
    }
    
    return result;
};

/**
 * 安全解析JSON，自动清理控制字符
 * 定义为全局函数，供其他脚本使用
 * @param {string} jsonStr - JSON字符串
 * @returns {Object|null} 解析后的JSON对象，失败返回null
 */
window.safeJSONParse = function(jsonStr) {
    if (!jsonStr || typeof jsonStr !== 'string') {
        return null;
    }
    
    const trimmed = jsonStr.trim();
    
    // 先尝试直接解析
    try {
        return JSON.parse(trimmed);
    } catch (e) {
        // 如果直接解析失败，尝试清理后解析
        try {
            const sanitized = window.sanitizeJSONString(trimmed);
            return JSON.parse(sanitized);
        } catch (e2) {
            // 尝试提取错误位置信息
            const errorMatch = e2.message.match(/position (\d+)/);
            const errorPos = errorMatch ? parseInt(errorMatch[1]) : -1;
            
            console.error('[safeJSONParse] JSON解析失败:', e2.message);
            console.error('[safeJSONParse] 原始字符串长度:', trimmed.length);
            console.error('[safeJSONParse] 原始字符串前500字符:', trimmed.substring(0, 500));
            
            if (errorPos > 0 && errorPos < trimmed.length) {
                const start = Math.max(0, errorPos - 100);
                const end = Math.min(trimmed.length, errorPos + 100);
                console.error('[safeJSONParse] 错误位置附近的内容 (位置 ' + errorPos + '):', 
                    trimmed.substring(start, end));
                console.error('[safeJSONParse] 错误位置标记: ' + 
                    ' '.repeat(Math.max(0, 100 - (errorPos - start))) + '^');
            }
            
            // 尝试更激进的修复方法
            try {
                // 方法1: 移除可能的尾随逗号
                let fixed = trimmed.replace(/,(\s*[}\]])/g, '$1');
                
                // 方法2: 使用更强大的控制字符清理（在字符串值内部转义所有控制字符）
                // 重新实现一个更可靠的版本
                fixed = window.sanitizeJSONString(fixed);
                
                // 方法3: 如果还是失败，使用更激进的方法：直接扫描并转义所有在字符串内部的控制字符
                let finalFixed = '';
                let inString = false;
                
                for (let i = 0; i < fixed.length; i++) {
                    const char = fixed[i];
                    const charCode = fixed.charCodeAt(i);
                    
                    // 处理反斜杠（转义字符）
                    if (char === '\\') {
                        if (i + 1 < fixed.length) {
                            const nextChar = fixed[i + 1];
                            if (nextChar === 'n' || nextChar === 'r' || nextChar === 't' || 
                                nextChar === '"' || nextChar === '\\' || nextChar === '/' ||
                                nextChar === 'b' || nextChar === 'f') {
                                // 标准转义序列
                                finalFixed += char + nextChar;
                                i++; // 跳过下一个字符
                                continue;
                            } else if (nextChar === 'u' && i + 5 < fixed.length) {
                                // \uXXXX转义序列
                                const hexChars = fixed.substring(i + 2, i + 6);
                                if (/^[0-9A-Fa-f]{4}$/.test(hexChars)) {
                                    finalFixed += fixed.substring(i, i + 6);
                                    i += 5; // 跳过\uXXXX
                                    continue;
                                }
                            }
                        }
                        // 无效的转义序列或字符串末尾的反斜杠
                        finalFixed += '\\\\';
                        continue;
                    }
                    
                    // 处理引号（字符串边界）
                    if (char === '"') {
                        // 检查前面是否有奇数个反斜杠
                        let backslashCount = 0;
                        let j = i - 1;
                        while (j >= 0 && fixed[j] === '\\') {
                            backslashCount++;
                            j--;
                        }
                        
                        if (backslashCount % 2 === 0) {
                            inString = !inString;
                        }
                        finalFixed += char;
                        continue;
                    }
                    
                    // 在字符串内部，转义所有未转义的控制字符
                    if (inString) {
                        if (charCode >= 0x00 && charCode <= 0x1F) {
                            // 控制字符，转义为\uXXXX
                            const hexCode = charCode.toString(16).padStart(4, '0').toUpperCase();
                            finalFixed += '\\u' + hexCode;
                        } else {
                            finalFixed += char;
                        }
                    } else {
                        // 在字符串外部，保留字符（但移除控制字符，除了空白字符）
                        if (charCode === 0x09 || charCode === 0x0A || charCode === 0x0D || charCode === 0x20 || charCode > 0x1F) {
                            finalFixed += char;
                        }
                    }
                }
                
                return JSON.parse(finalFixed);
            } catch (e3) {
                console.error('[safeJSONParse] 自动修复也失败:', e3.message);
                
                // 最后的尝试：如果错误位置已知，尝试在该位置附近修复
                const errorMatch = e3.message.match(/position (\d+)/);
                if (errorMatch) {
                    const errorPos = parseInt(errorMatch[1]);
                    console.error('[safeJSONParse] 尝试在错误位置附近修复...');
                    // 这里可以添加更具体的修复逻辑，但通常已经很难修复了
                }
                
                return null;
            }
        }
    }
};

// 注意：不创建本地const引用，避免重复声明错误
// 其他文件应该直接使用 window.safeJSONParse 或检查后创建本地引用

// 初始化全局状态（如果尚未初始化）
if (!window.APP_STATE) {
    window.APP_STATE = {
        // 模型相关
        models: [],
        currentModel: null,

        // 会话相关 - 重构为按功能分类管理
        sessions: [],              // 所有会话列表（保持向后兼容）
        currentSession: null,      // 当前活动会话（保持向后兼容）

        // 功能专用会话管理
        excelSession: null,        // Excel页面专用会话
        databaseSessions: {        // 数据库会话管理
            mysql: null,           // MySQL连接的会话
            sqlite: null,          // SQLite连接的会话
            current: null          // 当前活动的数据库会话
        },

        // 文件相关
        files: [],
        currentFile: null,

        // 消息相关
        messages: [],

        // 页面状态
        currentPage: 'model-chat',
        currentTab: 'file-preview',

        // 数据库表分页状态
        databases: {
            current: null,
            tablePagination: {
                currentPage: 1,
                totalPages: 1,
                totalRows: 0,
                pageSize: 50,
                currentTableName: null
            }
        }
    };
}

// 页面会话管理器 - 处理不同页面间的会话切换（重命名避免与auth.js中的SessionManager冲突）
window.PageSessionManager = {
    // 切换到Excel页面会话
    switchToExcelSession() {
        console.log('[PageSessionManager] 切换到Excel页面会话');
        APP_STATE.currentSession = APP_STATE.excelSession;
        console.log('当前会话状态:', APP_STATE.currentSession ? 'Excel会话' : '无会话');
    },

    // 切换到数据库页面会话
    switchToDatabaseSession() {
        console.log('[PageSessionManager] 切换到数据库页面会话');
        // 【修复】确保 databaseSessions 对象存在
        if (!APP_STATE.databaseSessions) {
            APP_STATE.databaseSessions = {
                mysql: null,
                sqlite: null,
                current: null
            };
        }
        APP_STATE.currentSession = APP_STATE.databaseSessions.current;
        console.log('当前会话状态:', APP_STATE.currentSession ? '数据库会话' : '无会话');
    },

    // 设置Excel会话
    setExcelSession(session) {
        console.log('[PageSessionManager] 设置Excel会话:', session?.id);
        APP_STATE.excelSession = session;
        if (APP_STATE.currentPage === 'chat') {
            this.switchToExcelSession();
        }
    },

    // 设置数据库会话
    setDatabaseSession(type, session) {
        console.log(`[PageSessionManager] 设置${type}数据库会话:`, session?.id);
        // 【修复】确保 databaseSessions 对象存在
        if (!APP_STATE.databaseSessions) {
            APP_STATE.databaseSessions = {
                mysql: null,
                sqlite: null,
                current: null
            };
        }
        APP_STATE.databaseSessions[type] = session;
        APP_STATE.databaseSessions.current = session;
        
        // 【修复】始终设置 APP_STATE.currentSession，无论当前页面是什么
        APP_STATE.currentSession = session;
        console.log('[PageSessionManager] 已设置 APP_STATE.currentSession:', session?.id);
        
        if (APP_STATE.currentPage === 'database') {
            this.switchToDatabaseSession();
        }
    },

    // 清空数据库会话
    clearDatabaseSession(type) {
        console.log(`[SessionManager] 清空${type}数据库会话`);
        // 【修复】确保 databaseSessions 对象存在
        if (!APP_STATE.databaseSessions) {
            APP_STATE.databaseSessions = {
                mysql: null,
                sqlite: null,
                current: null
            };
        }
        APP_STATE.databaseSessions[type] = null;
        if (APP_STATE.databaseSessions.current?.type === type) {
            APP_STATE.databaseSessions.current = null;
        }
        if (APP_STATE.currentPage === 'database') {
            this.switchToDatabaseSession();
        }
    },

    // 根据页面类型切换会话
    switchSessionByPage(pageName) {
        console.log(`[SessionManager] 根据页面类型切换会话: ${pageName}`);
        APP_STATE.currentPage = pageName;

        if (pageName === 'chat') {
            this.switchToExcelSession();
        } else if (pageName === 'database') {
            this.switchToDatabaseSession();
        }
    },

    // 获取当前页面对应的会话
    getCurrentSessionForPage(pageName) {
        if (pageName === 'chat') {
            return APP_STATE.excelSession;
        } else if (pageName === 'database') {
            // 【修复】确保 databaseSessions 对象存在
            if (!APP_STATE.databaseSessions) {
                APP_STATE.databaseSessions = {
                    mysql: null,
                    sqlite: null,
                    current: null
                };
            }
            return APP_STATE.databaseSessions.current;
        }
        return null;
    }
};

// 初始化全局配置（如果尚未初始化）
if (!window.CONFIG) {
    window.CONFIG = {
        API_BASE_URL: window.location.origin,
        FILE_SERVER: {
            HEALTH: '/fileServer/health',
            UPLOAD_FILE_INFO: '/fileServer/uploadFileInfo',
            UPLOAD: '/fileServer/upload',
            GET_FILE_INFO: '/fileServer/getFileInfo',
            DOWNLOAD: '/fileServer/downloadFile',
            DELETE: '/fileServer',
            FILE_LISTS: '/fileServer/fileLists',
            PREVIEW_EXCEL: '/fileServer/previewExcel'
        },
        MODEL_SERVER: {
            SESSIONS: '/api/sessions',
            MODELS: '/api/models',
            CREATE_SESSION: '/api/session',
            SEND_MESSAGE: '/api/message/async',
            DELETE_SESSION: '/api/session',
            SESSION_HISTORY: '/api/session'
        },
        UPLOAD: {
            MAX_FILE_SIZE: 10 * 1024 * 1024, // 10MB
            ALLOWED_TYPES: ['.xlsx']
        }
    };
}

// 应用初始化
async function initApp() {
    console.log('========== 开始初始化Chat2Excel应用 ==========');
    
    console.log('========================================');
    console.log('[initApp] ★★★ 应用初始化开始 ★★★');
    console.log('[initApp] 当前URL:', window.location.href);
    console.log('[initApp] localStorage.sessionId:', localStorage.getItem('sessionId'));
    console.log('[initApp] sessionStorage.justLoggedIn:', sessionStorage.getItem('justLoggedIn'));
    console.log('========================================');
    
    // 【修复】强制隐藏loading遮罩（防止页面一直显示加载中）
    // 使用多种方式确保遮罩被隐藏
    if (typeof hideLoading === 'function') {
        hideLoading();
    }
    // 直接操作DOM，确保遮罩被隐藏
    const overlay = document.getElementById('loading-overlay');
    if (overlay) {
        overlay.style.display = 'none';
        overlay.style.visibility = 'hidden';
        overlay.style.opacity = '0';
        overlay.style.pointerEvents = 'none';
        console.log('[initApp] 已强制隐藏loading遮罩');
    }
    
    try {
        // 0. 生成请求ID（API调用必需）
        if (!APP_STATE.user || !APP_STATE.user.requestId) {
            APP_STATE.user = {
                id: 'default_user',
                requestId: generateRequestId()
            };
            console.log('生成的请求ID:', APP_STATE.user.requestId);
        }
        
        // 【修复】1. 首先执行会话登录检查（必须在其他初始化之前）
        try {
            console.log('========== 开始会话登录检查 ==========');
            
            // 检查 SessionManager 是否已定义
            if (typeof SessionManager === 'undefined' || !SessionManager) {
                console.error('[initApp] SessionManager未定义，可能脚本加载顺序有问题');
                // 不跳转，继续执行，让用户看到错误信息
                throw new Error('SessionManager未定义');
            }
            
            const sessionId = SessionManager.getSessionId();
            console.log('[initApp] localStorage中的sessionId:', sessionId);
            
            if (!sessionId) {
                console.error('[initApp] 没有sessionId，直接跳转到登录页面');
                window.location.href = 'login.html';
                return;
            }
            
            const justLoggedIn = sessionStorage.getItem('justLoggedIn');
            console.log('[initApp] 检查登录标志 - justLoggedIn:', justLoggedIn);
            
            if (justLoggedIn === 'true') {
                console.log('[initApp] 检测到刚登录成功的标志，跳过session验证');
                sessionStorage.removeItem('justLoggedIn');
            } else {
                console.log('[initApp] 未检测到登录标志，需要验证session');
                const isLoggedIn = await SessionManager.trySessionLogin();
                console.log('[initApp] trySessionLogin返回结果:', isLoggedIn);
                
                if (!isLoggedIn) {
                    console.error('[initApp] 会话登录失败，直接跳转到登录页面');
                    window.location.href = 'login.html';
                    return;
                }
            }
            
            console.log('[initApp] 会话登录成功，继续初始化应用');
        } catch (error) {
            console.error('[initApp] 会话检查失败:', error);
            
            if (error.message && error.message.includes('SessionManager未定义')) {
                if (typeof showMessage === 'function') {
                    showMessage('应用初始化失败：SessionManager未定义，请刷新页面重试', 'error');
                }
                return;
            }
            
            console.error('[initApp] 发生异常，直接跳转到登录页面');
            window.location.href = 'login.html';
            return;
        }
        
        // 2. 添加Markdown样式
        try {
            addMarkdownStyles();
            console.log('Markdown样式添加成功');
        } catch (error) {
            console.error('添加Markdown样式失败:', error);
        }
        
        // 3. 检查文件服务状态（使用更健壮的健康检查）
        try {
            const healthCheck = await FileAPI.checkHealth();
            console.log('文件服务健康检查结果:', healthCheck);
        } catch (error) {
            console.warn('文件服务健康检查失败，但可能只是暂时性问题:', error);
        }
        
        // 4. 获取模型列表（仅获取模型列表，不获取文件列表和会话列表）
        try {
            await updateModelsList();
            console.log('模型列表更新成功');
        } catch (error) {
            console.error('更新模型列表失败，但不影响应用启动:', error);
        }
        
        // 5. 初始化页面导航
        try {
            initPageNavigation();
            console.log('页面导航初始化成功');
        } catch (error) {
            console.error('页面导航初始化失败:', error);
        }
        
        // 5.1. 初始化数据库连接页面
        try {
            initDatabaseConnection();
            console.log('数据库连接页面初始化成功');
        } catch (error) {
            console.error('数据库连接页面初始化失败:', error);
        }
        
        // 6. 初始化聊天功能
        try {
            if (typeof window.initChat === 'function') {
                window.initChat();
                console.log('聊天功能初始化成功');
            } else {
                console.warn('initChat函数未定义，请确保chat.js已加载');
            }
        } catch (error) {
            console.error('聊天功能初始化失败:', error);
        }
        
        // 6.1. 初始化数据库对话页面聊天功能
        try {
            initDatabaseChat();
            console.log('数据库对话页面聊天功能初始化成功');
        } catch (error) {
            console.error('数据库对话页面聊天功能初始化失败:', error);
        }
        
        // 7. 初始化文件功能
        try {
            if (typeof initFileFunctions === 'function') {
                initFileFunctions();
                console.log('文件功能初始化成功');
            }
        } catch (error) {
            console.error('文件功能初始化失败:', error);
        }
        
        console.log('========== 应用初始化完成 ==========');
        
        // 8. 加载用户信息（在session登录成功后）
        try {
            if (typeof loadUserInfo === 'function') {
                await loadUserInfo();
                console.log('用户信息加载成功');
            }
        } catch (error) {
            console.error('用户信息加载失败:', error);
        }
        
        // 【修复】强制隐藏loading遮罩（确保初始化完成后遮罩被隐藏）
        if (typeof hideLoading === 'function') {
            hideLoading();
        }
        // 双重保险：直接操作DOM
        const overlay = document.getElementById('loading-overlay');
        if (overlay) {
            overlay.style.display = 'none';
            overlay.style.visibility = 'hidden';
            overlay.style.opacity = '0';
            overlay.style.pointerEvents = 'none';
        }
        
    } catch (error) {
        console.error('========== 应用初始化出现严重错误 ==========');
        console.error('错误详情:', error);
        console.error('错误堆栈:', error.stack);
        
        // 【修复】强制隐藏loading遮罩（确保错误时遮罩也被隐藏）
        if (typeof hideLoading === 'function') {
            hideLoading();
        }
        // 双重保险：直接操作DOM
        const overlay = document.getElementById('loading-overlay');
        if (overlay) {
            overlay.style.display = 'none';
            overlay.style.visibility = 'hidden';
            overlay.style.opacity = '0';
            overlay.style.pointerEvents = 'none';
        }
        
        // 只在严重错误时才显示消息
        if (typeof showMessage === 'function') {
            showMessage('应用初始化出现问题，部分功能可能不可用', 'warning');
        }
    }
}

// 更新模型列表
async function updateModelsList() {
    try {
        const response = await ModelAPI.getModels();
        console.log('获取模型列表响应:', response);
        if (response && response.success) {
            const list = (response.result && response.result.modelList) || [];
            APP_STATE.models = list.map(m => ({
                modelName: m.modelName,
                modelDescription: m.modelDesc || m.modelName
            }));
            
            console.log('模型列表:', APP_STATE.models);
            
            // 设置默认模型
            const defaultModel = APP_STATE.models.find(m => m.modelName === 'deepseek') || 
                               APP_STATE.models[0];
            
            if (defaultModel) {
                APP_STATE.currentModel = defaultModel;
                updateModelSelector();
            }
        } else {
            console.warn('模型列表响应无效，使用默认配置');
            // 设置默认模型
            APP_STATE.models = [{ modelName: 'deepseek', modelDescription: 'DeepSeek模型' }];
            APP_STATE.currentModel = APP_STATE.models[0];
            updateModelSelector();
        }
    } catch (error) {
        console.error('获取模型列表失败:', error);
        // 设置默认模型，不阻止初始化流程
        APP_STATE.models = [{ modelName: 'deepseek', modelDescription: 'DeepSeek模型' }];
        APP_STATE.currentModel = APP_STATE.models[0];
        updateModelSelector();
    }
}

// 更新会话列表
async function updateSessionsList() {
    try {
        const response = await ModelAPI.getSessions();
        console.log('获取会话列表响应:', response);
        if (response.success) {
            const list = (response.result && response.result.chatSessionLists) || [];
            APP_STATE.sessions = list.map(s => ({
                id: s.chatSessionId,
                model: s.modelName,
                title: s.title,
                created_at: s.createdAt,
                updated_at: s.updatedAt,
                message_count: s.messageCount,
                first_user_message_content: s.firstUserMessageContent,
                sessionType: s.sessionType,
                dbConnectionInfo: s.dbConnectionInfo
            }));
            console.log('会话列表数量:', APP_STATE.sessions.length);
            updateSessionsListDisplay();
        }
    } catch (error) {
        console.error('获取会话列表失败:', error);
    }
}

// 更新文件列表
async function updateFilesList() {
    console.log('开始更新文件列表...');
    try {
        console.log('调用FileAPI.getFileList()...');
        const response = await FileAPI.getFileList();
        console.log('API响应结果:', response);
        
        if (response.success) {
            let filesData = response.result;
            
            // 处理嵌套的文件列表结构
            if (filesData && filesData.fileList && Array.isArray(filesData.fileList)) {
                // 如果返回的是 {fileList: [...]} 结构
                APP_STATE.files = filesData.fileList;
            } else if (Array.isArray(filesData)) {
                // 如果直接返回文件数组
                APP_STATE.files = filesData;
            } else if (filesData && typeof filesData === 'object') {
                // 如果result是对象，尝试提取数组
                APP_STATE.files = Object.values(filesData);
            } else {
                APP_STATE.files = [];
            }
            
            console.log('成功获取文件列表，文件数量:', APP_STATE.files.length);
            console.log('文件列表数据:', APP_STATE.files);
            updateFilesListDisplay();
        } else {
            console.error('获取文件列表失败:', response.message);
            showMessage('获取文件列表失败: ' + response.message, 'error');
        }
    } catch (error) {
        console.error('更新文件列表失败:', error);
        showMessage('获取文件列表失败，请检查网络连接', 'error');
    }
}

/**
 * 判断列类型是否为BLOB类型
 * @param {string} columnType - 列类型名称
 * @returns {boolean} 是否为BLOB类型
 */
function isBlobType(columnType) {
    if (!columnType || typeof columnType !== 'string') {
        return false;
    }
    const upperType = columnType.toUpperCase();
    return upperType.includes('BLOB') || upperType.includes('BINARY') || upperType.includes('VARBINARY');
}

/**
 * 将Base64字符串解码为十六进制字符串（以空格间隔）
 * @param {string} base64String - Base64编码的字符串
 * @returns {string} 十六进制字符串，字节之间以空格分隔
 */
function decodeBase64ToHex(base64String) {
    if (!base64String || typeof base64String !== 'string') {
        return '';
    }
    try {
        const decoded = atob(base64String);
        const bytes = new Uint8Array(decoded.length);
        for (let i = 0; i < decoded.length; i++) {
            bytes[i] = decoded.charCodeAt(i);
        }
        return Array.from(bytes)
            .map(b => b.toString(16).padStart(2, '0').toUpperCase())
            .join(' ');
    } catch (e) {
        console.error('[decodeBase64ToHex] 解码失败:', e);
        return base64String;
    }
}

// 更新模型选择器
function updateModelSelector() {
    const modelSelect = document.getElementById('model-select');
    if (!modelSelect) {
        console.warn('模型选择器元素不存在');
        return;
    }
    
    if (!APP_STATE.currentModel || !APP_STATE.models || APP_STATE.models.length === 0) {
        console.warn('当前模型或模型列表为空');
        return;
    }
    
    modelSelect.innerHTML = '';
    
    APP_STATE.models.forEach(model => {
        const option = document.createElement('option');
        option.value = model.modelName;
        // 只显示模型名称
        option.textContent = model.modelName;
        option.selected = model.modelName === APP_STATE.currentModel.modelName;
        modelSelect.appendChild(option);
    });
    
    // 移除之前的事件监听器，避免重复绑定
    const newModelSelect = modelSelect.cloneNode(true);
    modelSelect.parentNode.replaceChild(newModelSelect, modelSelect);
    
    // 添加选择事件
    newModelSelect.addEventListener('change', (e) => {
        const selectedModel = APP_STATE.models.find(m => m.modelName === e.target.value);
        if (selectedModel) {
            APP_STATE.currentModel = selectedModel;
            console.log('切换模型到:', selectedModel.modelName);
        }
    });
}

// 更新会话列表显示
function updateSessionsListDisplay() {
    const sessionsList = document.getElementById('sessions-list');
    if (!sessionsList) return;
    
    sessionsList.innerHTML = '';
    
    if (APP_STATE.sessions.length === 0) {
        sessionsList.innerHTML = '<div class="empty-state">暂无会话记录</div>';
        return;
    }
    
    APP_STATE.sessions.forEach(session => {
        const sessionItem = document.createElement('div');
        sessionItem.className = 'session-item';
        
        sessionItem.innerHTML = `
            <div class="session-info">
                <div class="session-title">${session.title || '未命名会话'}</div>
                <div class="session-time">${formatTime(session.created_at * 1000)}</div>
            </div>
            <button class="session-delete">删除</button>
        `;
        
        const deleteBtn = sessionItem.querySelector('.session-delete');
        deleteBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            deleteSession(session.id);
        });
        
        sessionItem.addEventListener('click', () => {
            switchToSession(session);
        });
        
        sessionsList.appendChild(sessionItem);
    });
}

// 更新文件列表显示
function updateFilesListDisplay() {
    console.log('开始更新文件列表显示...');
    const filesList = document.getElementById('files-list');
    
    if (!filesList) {
        console.error('files-list元素未找到！');
        return;
    }
    
    // 确保APP_STATE.files是一个数组
    if (!Array.isArray(APP_STATE.files)) {
        console.error('APP_STATE.files不是数组:', APP_STATE.files);
        APP_STATE.files = [];
    }
    
    console.log('找到files-list元素，文件数量:', APP_STATE.files.length);
    console.log('文件列表数据类型:', typeof APP_STATE.files);
    
    filesList.innerHTML = '';
    
    if (APP_STATE.files.length === 0) {
        filesList.innerHTML = '<div class="empty-state">暂无文件记录</div>';
        console.log('显示空状态');
        return;
    }
    
    try {
        APP_STATE.files.forEach((file, index) => {
            // 确保每个文件对象都有必要的属性
            if (!file || typeof file !== 'object') {
                console.warn(`文件数据格式错误，索引 ${index}:`, file);
                return;
            }
            
            const fileItem = document.createElement('div');
            fileItem.className = 'file-item';
            
            // 安全地访问文件属性
            const fileName = file.fileName || file.name || '未知文件';
            const fileId = file.fileId || file.id || index;
            const fileSize = file.fileSize || file.size || 0;
            
            // 格式化上传时间
            const uploadTime = file.uploadTime || file.uploadTime || 0;
            const uploadDate = new Date(uploadTime * 1000);
            const formattedTime = uploadDate.toLocaleString('zh-CN', {
                year: 'numeric',
                month: '2-digit',
                day: '2-digit',
                hour: '2-digit',
                minute: '2-digit'
            });
            
            fileItem.innerHTML = `
                <div class="file-icon">📊</div>
                <div class="file-info">
                    <div class="file-name">
                        <span class="file-name-text">${fileName}</span>
                        <span class="file-size">${formatFileSize(fileSize)}</span>
                        <span class="file-upload-time">${formattedTime}</span>
                    </div>
                </div>
                <div class="file-actions">
                    <button class="btn-preview" onclick="previewFile('${fileId}')">预览</button>
                    <button class="btn-download" onclick="downloadFile('${fileId}')">下载</button>
                    <button class="btn-delete" onclick="deleteFile('${fileId}')">删除</button>
                </div>
            `;
            
            filesList.appendChild(fileItem);
        });
        
        console.log('文件列表显示更新完成');
    } catch (error) {
        console.error('更新文件列表显示时出错:', error);
        filesList.innerHTML = '<div class="empty-state">文件列表显示错误</div>';
    }
}

// 切换到指定会话
async function switchToSession(session) {
    try {
        console.log('=== 开始切换会话 ===');
        console.log('会话信息:', session);
        
        // 获取会话历史
        const historyResponse = await ModelAPI.getSessionHistory(session.id);
        console.log('会话历史响应:', historyResponse);
        
        if (historyResponse.success) {
            // 优先使用会话列表中的sessionType，如果不存在则从historyResponse中获取
            const sessionType = session.sessionType || (historyResponse.result && historyResponse.result.sessionType) || 'excel';
            console.log('会话类型:', sessionType, '(来源:', session.sessionType ? '会话列表' : '历史响应', ')');
            
            // 根据会话类型切换到不同页面
            if (sessionType === 'database') {
                // 数据库场景：切换到数据库页面
                await switchToDatabaseSession(session, historyResponse);
            } else {
                // Excel场景：切换到Excel页面
                await switchToExcelSession(session, historyResponse);
            }
        }
    } catch (error) {
        console.error('切换会话失败:', error);
        showMessage('切换会话失败: ' + error.message, 'error');
    }
}

// 切换到Excel会话
async function switchToExcelSession(session, historyResponse) {
    try {
        console.log('=== 切换到Excel会话 ===');
        
        // 清空文件预览、结果分析、消息列表
        console.log('清空文件预览、结果分析、消息列表');
        
        // 1. 清空文件预览
        if (typeof clearFilePreview === 'function') {
            clearFilePreview();
        } else {
            // 手动清空文件预览
            const uploadPrompt = document.getElementById('upload-prompt');
            const excelPreview = document.getElementById('excel-preview');
            const excelTable = document.getElementById('excel-table');
            const sheetTabs = document.getElementById('sheet-tabs');
            const excelFileName = document.getElementById('excel-file-name');
            
            if (uploadPrompt) uploadPrompt.style.display = '';
            if (excelPreview) excelPreview.style.display = 'none';
            if (excelTable) excelTable.innerHTML = '';
            if (sheetTabs) sheetTabs.innerHTML = '';
            if (excelFileName) excelFileName.textContent = '';
            
            APP_STATE.currentFile = null;
        }
        
        // 2. 清空结果分析
        const resultContent = document.getElementById('result-content');
        if (resultContent) {
            resultContent.innerHTML = '';
        }
        
        // 3. 清空消息列表
        const chatMessages = document.getElementById('chat-messages');
        if (chatMessages) {
            chatMessages.innerHTML = '';
        }
        APP_STATE.messages = [];
        
        const messages = (historyResponse.result && historyResponse.result.messageList) || [];
        console.log('历史消息数量:', messages.length);
        console.log('历史消息:', messages);
        
        // 确保会话对象有正确的结构
        APP_STATE.currentSession = {
            id: session.id,
            title: session.title || '未命名会话',
            created_at: session.created_at || Date.now() / 1000,
            messages: messages,
            fileId: null,
            sessionType: 'excel'
        };
        APP_STATE.messages = APP_STATE.currentSession.messages;
        console.log('当前会话已设置:', APP_STATE.currentSession);
        console.log('会话ID:', APP_STATE.currentSession.id);
        // 设置当前会话的文件ID
        const sessionFileId = historyResponse.result && historyResponse.result.fileId;
        if (sessionFileId) {
            APP_STATE.currentSession.fileId = sessionFileId;
        }
        console.log('会话关联的文件ID:', APP_STATE.currentSession.fileId);
        console.log('消息数量:', APP_STATE.currentSession.messages.length);
        
        // 检查会话是否关联了文件
        if (sessionFileId) {
            // 预览关联的文件
            try {
                const previewResponse = await FileAPI.previewExcel(sessionFileId);
                if (previewResponse.success) {
                    displayExcelPreview(previewResponse.result);
                    
                    // 设置当前文件
                    APP_STATE.currentFile = {
                        id: sessionFileId,
                        name: previewResponse.result.fileName,
                        size: previewResponse.result.fileSize,
                        data: previewResponse.result
                    };
                    
                    // 切换到文件预览标签页
                    switchTab('file-preview');
                }
            } catch (error) {
                console.error('预览关联文件失败:', error);
                clearFilePreview();
            }
        } else {
            // 会话未关联文件，清空文件预览区域
            console.log('会话未关联文件，清空文件预览区域');
            clearFilePreview();
        }
        
        // 切换到chat页面
        console.log('切换到 chat 页面');
        switchPage('chat');
        
        // 等待页面切换完成后再更新消息显示
        setTimeout(() => {
            console.log('开始更新消息显示');
            updateMessagesDisplay();
            console.log('消息显示更新完成');
        }, 100);
        
        // 启用发送按钮
        const sendBtn = document.getElementById('send-btn');
        if (sendBtn) {
            sendBtn.disabled = false;
        }
        
        // 更新上传按钮状态
        if (typeof updateUploadButtonState === 'function') {
            updateUploadButtonState();
            console.log('上传按钮状态已更新（切换会话）');
        }
        
        console.log('=== Excel会话切换完成 ===');
    } catch (error) {
        console.error('切换到Excel会话失败:', error);
        showMessage('切换到Excel会话失败: ' + error.message, 'error');
    }
}

// 切换到数据库会话
async function switchToDatabaseSession(session, historyResponse) {
    try {
        console.log('=== 切换到数据库会话 ===');
        
        // 优先使用会话列表中的dbConnectionInfo，如果不存在则从historyResponse中获取
        const dbConnectionInfo = session.dbConnectionInfo || (historyResponse.result && historyResponse.result.dbConnectionInfo);
        const dbConnectId = historyResponse.result && historyResponse.result.dbConnectId;
        
        console.log('数据库会话连接信息:', dbConnectionInfo, '(来源:', session.dbConnectionInfo ? '会话列表' : '历史响应', ')');
        
        if (!dbConnectionInfo) {
            showMessage('该会话的数据库连接信息已丢失，请重新连接数据库', 'error');
            // 跳转到数据库连接页面
            switchPage('database');
            return;
        }
        
        try {
            // 解析数据库连接信息
            const connectionInfo = JSON.parse(dbConnectionInfo);
            console.log('解析后的连接信息:', connectionInfo);
            
            // 判断数据库类型
            let dbType = 'MySQL';
            let connectionData = {};
            
            // 通过字段特征判断数据库类型
            if (connectionInfo.fileId || connectionInfo.fileName || connectionInfo.filePath) {
                // SQLite数据库
                dbType = 'SQLite';
                connectionData = {
                    type: 'SQLite',
                    SQLite: connectionInfo
                };
            } else if (connectionInfo.host && connectionInfo.port && connectionInfo.name) {
                // MySQL数据库
                dbType = 'MySQL';
                connectionData = {
                    type: 'MySQL',
                    MySQL: connectionInfo
                };
            } else {
                throw new Error('无法识别数据库类型');
            }
            
            console.log('构建的连接数据:', connectionData);
            
            // 尝试重新连接数据库
            const connectResponse = await connectDatabase(connectionData);
            
            if (connectResponse && connectResponse.success && connectResponse.result) {
                const connectionId = connectResponse.result.connectionId;
                console.log('数据库重新连接成功，connectionId:', connectionId);
                
                // 获取表列表和数据
                const tablesRes = await getTableList(connectionId);
                const tables = (tablesRes && tablesRes.success && tablesRes.result && tablesRes.result.tables) 
                    ? tablesRes.result.tables : [];
                
                let tableSchema = null;
                let tableData = [];
                if (tables.length > 0) {
                    const firstTableRes = await getTableData(connectionId, tables[0]);
                    if (firstTableRes && firstTableRes.success && firstTableRes.result) {
                        tableSchema = { ...firstTableRes.result.tableSchema, tableName: tables[0] };
                        tableData = firstTableRes.result.tableSchema.tableData || [];
                    }
                }
                
                // 更新APP_STATE
                if (!APP_STATE.databases) {
                    APP_STATE.databases = {};
                }
                
                // 使用正确的数据库类型（MySQL或SQLite）
                const dbTypeLower = dbType.toLowerCase(); // mysql 或 sqlite
                if (!APP_STATE.databases[dbTypeLower]) {
                    APP_STATE.databases[dbTypeLower] = {};
                }
                
                APP_STATE.databases[dbTypeLower].connectionId = connectionId;
                APP_STATE.databases[dbTypeLower].connectionInfo = connectionInfo;
                APP_STATE.databases[dbTypeLower].dbType = dbType;
                APP_STATE.databases[dbTypeLower].tables = tables;
                APP_STATE.databases[dbTypeLower].tableSchema = tableSchema;
                APP_STATE.databases[dbTypeLower].currentTable = tables[0] || null;
                APP_STATE.databases.current = APP_STATE.databases[dbTypeLower];
                
                // 设置当前会话
                const messages = (historyResponse.result && historyResponse.result.messageList) || [];
                APP_STATE.currentSession = {
                    id: session.id,
                    title: session.title || '未命名会话',
                    created_at: session.created_at || Date.now() / 1000,
                    messages: messages,
                    sessionType: 'database',
                    dbConnectId: connectionId,
                    dbConnectionInfo: connectionInfo
                };
                
                // 切换到数据库聊天页面
                switchToDatabaseChatPage({
                    connectionId,
                    tables,
                    tableSchema,
                    tableData
                });
                
                // 更新导航栏选中状态到智能DB助手
                const navItems = document.querySelectorAll('.nav-item');
                navItems.forEach(nav => nav.classList.remove('active'));
                const dbNav = document.querySelector('.nav-item[data-page="database"]');
                if (dbNav) {
                    dbNav.classList.add('active');
                }
                
                // 更新已连接数据库显示
                updateConnectedDatabaseDisplay();
                
                // 切换到对应的数据库标签页（MySQL或SQLite）
                const dbTabBtns = document.querySelectorAll('.db-tab-btn');
                dbTabBtns.forEach(btn => btn.classList.remove('active'));
                const dbTabBtn = document.querySelector(`.db-tab-btn[data-db-type="${dbTypeLower}"]`);
                if (dbTabBtn) {
                    dbTabBtn.classList.add('active');
                    switchDatabaseTab(dbTypeLower);
                }
                
                // 更新消息显示
                setTimeout(() => {
                    updateDBMessagesDisplay();
                }, 100);
                
                showMessage('数据库连接成功，已恢复会话', 'success');
            } else {
                throw new Error(connectResponse?.message || '数据库连接失败');
            }
        } catch (error) {
            console.error('恢复数据库连接失败:', error);
            showMessage('恢复数据库连接失败: ' + error.message + '，请重新连接数据库', 'error');
            switchPage('database');
        }
        
        console.log('=== 数据库会话切换完成 ===');
    } catch (error) {
        console.error('切换到数据库会话失败:', error);
        showMessage('切换到数据库会话失败: ' + error.message, 'error');
    }
}

// 更新消息显示
function updateMessagesDisplay() {
    const chatMessages = document.getElementById('chat-messages');
    if (!chatMessages) {
        console.error('找不到 chat-messages 容器');
        return;
    }
    
    chatMessages.innerHTML = '';
    
    // 【修复】优先使用 currentSession.messages，如果没有则使用 APP_STATE.messages
    const messagesToDisplay = (APP_STATE.currentSession && APP_STATE.currentSession.messages) 
        ? APP_STATE.currentSession.messages 
        : APP_STATE.messages;
    
    console.log('开始显示历史消息，消息数量:', messagesToDisplay.length);
    console.log('原始消息顺序:');
    messagesToDisplay.forEach((msg, i) => {
        console.log(`  [${i}] id: ${msg.id}, timestamp: ${msg.timestamp}, role: ${msg.role}, has TITLE: ${msg.content.includes('<TITLE')}, has CHART_DATA: ${msg.content.includes('<CHART_DATA>')}`);
    });
    
    // 【修复】按时间戳排序消息（升序：从早到晚）
    // 特殊处理：对于 timestamp 相同的 assistant 消息，按内容特征排序
    // 包含 TITLE 的应该在前，包含 CHART_DATA 的应该在后
    const sortedMessages = [...messagesToDisplay].sort((a, b) => {
        const timeDiff = (a.timestamp || 0) - (b.timestamp || 0);
    
        // 如果时间戳不同，按时间戳排序
        if (timeDiff !== 0) {
            return timeDiff;
        }
        
        // 时间戳相同，检查是否都是 assistant 消息
        if (a.role === 'assistant' && b.role === 'assistant') {
            const aHasTitle = a.content.includes('<TITLE');
            const bHasTitle = b.content.includes('<TITLE');
            const aHasChartData = a.content.includes('<CHART_DATA>') || a.content.includes('chartType');
            const bHasChartData = b.content.includes('<CHART_DATA>') || b.content.includes('chartType');
    
            // TITLE 消息应该在前，CHART_DATA 消息应该在后
            if (aHasTitle && !bHasTitle) return -1;
            if (!aHasTitle && bHasTitle) return 1;
            if (aHasChartData && !bHasChartData) return 1;
            if (!aHasChartData && bHasChartData) return -1;
        }
        
        // 其他情况保持原顺序
        return 0;
    });
    
    console.log('排序后消息顺序:');
    sortedMessages.forEach((msg, i) => {
        console.log(`  [${i}] id: ${msg.id}, timestamp: ${msg.timestamp}, role: ${msg.role}, has TITLE: ${msg.content.includes('<TITLE')}, has CHART_DATA: ${msg.content.includes('<CHART_DATA>')}`);
    });
    
    sortedMessages.forEach((message, index) => {
        console.log(`========== 消息 ${index} ==========`);
        console.log('  id:', message.id);
        console.log('  role:', message.role);
        console.log('  timestamp:', message.timestamp);
        console.log('  content (前100字符):', message.content.substring(0, 100));
        console.log('  包含 TITLE:', message.content.includes('<TITLE'));
        console.log('  包含 TASKS:', message.content.includes('<TASKS'));
        console.log('  包含 CHART_DATA:', message.content.includes('<CHART_DATA>'));
        console.log('  包含 chartType:', message.content.includes('chartType'));
        console.log('  包含 taskStatus:', message.content.includes('taskStatus'));
        displayHistoryMessage(message);
    });
    
    // 检查DOM中消息的实际顺序
    setTimeout(() => {
        console.log('========== DOM中消息的实际顺序 ==========');
        const messageElements = chatMessages.querySelectorAll('.message');
        messageElements.forEach((el, index) => {
            if (el.style.display !== 'none') {
                console.log(`  [${index}] id=${el.dataset.messageId}, timestamp=${el.dataset.timestamp}, role=${el.className}`);
            }
        });
    }, 50);
    
    // 滚动到底部
    setTimeout(() => {
        if (chatMessages.scrollHeight > chatMessages.clientHeight) {
            chatMessages.scrollTop = chatMessages.scrollHeight;
        }
    }, 100);
    
    // 同时更新结果分析区域（传递排序后的消息）
    updateResultAnalysisFromHistory(sortedMessages);
}

// 更新数据库页面的历史消息显示
function updateDBMessagesDisplay() {
    const dbChatMessages = document.getElementById('db-chat-messages');
    if (!dbChatMessages) {
        console.error('找不到 db-chat-messages 容器');
        return;
    }
    
    dbChatMessages.innerHTML = '';
    
    // 优先使用 currentSession.messages，如果没有则使用 APP_STATE.messages
    const messagesToDisplay = (APP_STATE.currentSession && APP_STATE.currentSession.messages) 
        ? APP_STATE.currentSession.messages 
        : APP_STATE.messages;
    
    console.log('开始显示数据库历史消息，消息数量:', messagesToDisplay.length);
    
    // 按时间戳排序消息（升序：从早到晚）
    const sortedMessages = [...messagesToDisplay].sort((a, b) => {
        return (a.timestamp || 0) - (b.timestamp || 0);
    });
    
    sortedMessages.forEach((message, index) => {
        console.log(`========== 数据库消息 ${index} ==========`);
        console.log('  id:', message.id);
        console.log('  role:', message.role);
        console.log('  timestamp:', message.timestamp);
        displayDBHistoryMessage(message);
    });
    
    // 滚动到底部
    setTimeout(() => {
        if (dbChatMessages.scrollHeight > dbChatMessages.clientHeight) {
            dbChatMessages.scrollTop = dbChatMessages.scrollHeight;
        }
    }, 100);
    
    // 更新数据库页面的结果分析区域
    updateDBResultAnalysisFromHistory(sortedMessages);
}

// 显示数据库历史消息
function displayDBHistoryMessage(message) {
    const dbChatMessages = document.getElementById('db-chat-messages');
    if (!dbChatMessages) return;
    
    const messageEl = document.createElement('div');
    messageEl.className = `message ${message.role}`;
    messageEl.dataset.messageId = message.id;
    messageEl.dataset.timestamp = message.timestamp;
    
    console.log(`  → 添加数据库消息到DOM: id=${message.id}, timestamp=${message.timestamp}, role=${message.role}`);
    
    const messageContent = document.createElement('div');
    messageContent.className = 'message-content';
    
    if (message.role === 'user') {
        // 检查是否是系统消息（SQL结果提示）- 不显示
        const isSystemMessage = message.content.includes('你是一个数据分析专家') || 
                               message.content.includes('SQL查询结果') ||
                               message.content.includes('任务背景');
        
        if (isSystemMessage) {
            console.log('跳过系统消息（SQL结果）');
            messageEl.style.display = 'none';
            dbChatMessages.appendChild(messageEl);
            return;
        }
        
        // 用户消息：过滤系统提示词
        const filteredContent = filterSystemPrompt(message.content);
        messageContent.textContent = filteredContent;
    } else if (message.role === 'assistant') {
        // 助手消息：解析特殊标签并格式化显示
        const content = message.content;
        
        // 检查是否包含特殊标签
        const hasTitle = content.includes('<TITLE');
        const hasTasks = content.includes('<TASKS');
        const hasChartData = content.includes('<CHART_DATA>') || content.includes('chartType');
        
        // 跳过只有 taskStatus 的中间消息
        if (!hasTitle && !hasTasks && !hasChartData && content.includes('taskStatus')) {
            console.log('  ⊗ 跳过中间状态消息（只有 taskStatus）');
            messageEl.style.display = 'none';
            dbChatMessages.appendChild(messageEl);
            return;
        }
        
        if (hasTitle || hasTasks || hasChartData) {
            // 使用格式化显示（与Excel页面一致）
            console.log('  → 使用格式化显示');
            displayFormattedMessage(messageContent, content);
        } else {
            // 普通文本
            console.log('  → 使用普通文本显示');
            messageContent.innerHTML = `<div class="markdown-content">${content.replace(/\n/g, '<br>')}</div>`;
        }
    }
    
    messageEl.appendChild(messageContent);
    
    // 添加时间戳
    const messageTime = document.createElement('div');
    messageTime.className = 'message-time';
    messageTime.textContent = formatTime(message.timestamp * 1000);
    messageEl.appendChild(messageTime);
    
    dbChatMessages.appendChild(messageEl);
}

// 显示历史消息（格式化显示）
function displayHistoryMessage(message) {
    const chatMessages = document.getElementById('chat-messages');
    if (!chatMessages) return;
        
        const messageEl = document.createElement('div');
        messageEl.className = `message ${message.role}`;
    messageEl.dataset.messageId = message.id;
    messageEl.dataset.timestamp = message.timestamp;
    
    console.log(`  → 添加消息到DOM: id=${message.id}, timestamp=${message.timestamp}, role=${message.role}`);
    
    const messageContent = document.createElement('div');
    messageContent.className = 'message-content';
        
        if (message.role === 'user') {
        // 检查是否是系统消息（SQL结果提示）- 不显示
        const isSystemMessage = message.content.includes('你是一个数据分析专家') || 
                               message.content.includes('SQL查询结果') ||
                               message.content.includes('任务背景');
            
            if (isSystemMessage) {
            console.log('跳过系统消息（SQL结果）');
            messageEl.style.display = 'none';  // 隐藏该消息
            chatMessages.appendChild(messageEl);
            return;
            }
            
            // 用户消息：过滤系统提示词
            const filteredContent = filterSystemPrompt(message.content);
            messageContent.textContent = filteredContent;
        } else if (message.role === 'assistant') {
        // 助手消息：解析特殊标签并格式化显示
            const content = message.content;
            
        // 检查是否包含特殊标签
        const hasTitle = content.includes('<TITLE');
        const hasTasks = content.includes('<TASKS');
        const hasChartData = content.includes('<CHART_DATA>') || content.includes('chartType');
        
        // 【修复】跳过只有 taskStatus 的中间消息（既没有 TITLE/TASKS，也没有 CHART_DATA，但包含 taskStatus）
        if (!hasTitle && !hasTasks && !hasChartData && content.includes('taskStatus')) {
            console.log('  ⊗ 跳过中间状态消息（只有 taskStatus）');
            messageEl.style.display = 'none';
            chatMessages.appendChild(messageEl);
            return;
        }
        
        if (hasTitle || hasTasks || hasChartData) {
            // 使用格式化显示
            console.log('  → 使用格式化显示');
            displayFormattedMessage(messageContent, content);
            } else {
            // 普通文本
            console.log('  → 使用普通文本显示');
            messageContent.innerHTML = `<div class="markdown-content">${content.replace(/\n/g, '<br>')}</div>`;
        }
                }
                
                messageEl.appendChild(messageContent);
        
        // 添加时间戳
        const messageTime = document.createElement('div');
        messageTime.className = 'message-time';
        messageTime.textContent = formatTime(message.timestamp * 1000);
        messageEl.appendChild(messageTime);
        
        chatMessages.appendChild(messageEl);
}

// 过滤系统提示词
function filterSystemPrompt(content) {
    if (!content) return '';
    
    // 提取"用户问题:"之后的内容
    const match = content.match(/用户问题\s*[:：]\s*(.+?)(?:\n\n|$)/s);
    if (match) {
        return match[1].trim();
    }
    
    return content;
}

// 格式化显示助手消息
function displayFormattedMessage(container, content) {
    // 1. 显示标题
    const titleMatch = content.match(/<TITLE_START>(.*?)<TITLE_END>/s) || 
                        content.match(/<TITLE>(.*?)<\/TITLE>/s);
        if (titleMatch) {
        const title = titleMatch[1].trim();
        const titleDiv = document.createElement('div');
        titleDiv.className = 'analysis-title';
        titleDiv.innerHTML = `<h3>📊 ${title}</h3>`;
        container.appendChild(titleDiv);
        }
    
    // 2. 显示任务列表
    const tasksMatch = content.match(/<TASKS_START>(.*?)<TASKS_END>/s) || 
                       content.match(/<TASKS>(.*?)<\/TASKS>/s);
    if (tasksMatch) {
        const tasksText = tasksMatch[1].trim();
        const taskLines = tasksText.split('\n')
            .map(line => line.trim())
            .filter(line => {
                // 支持多种任务编号格式
                return line.match(/^\d+[\s]*[\.。\)）]/) || line.match(/^[\(（]\d+[\)）]/);
            });
        
        if (taskLines.length > 0) {
            const tasksDiv = document.createElement('div');
            tasksDiv.className = 'task-list-container';
            tasksDiv.innerHTML = '<h4>📋 分析任务：</h4>';
            
            const taskList = document.createElement('ul');
            taskList.className = 'task-list';
            
            // 提取任务详情（在TASKS结束后、SQL/CHART_DATA之前的内容）
            const taskDetailsMap = {};
            let tasksEndIndex = content.indexOf('<TASKS_END>');
            if (tasksEndIndex === -1) {
                tasksEndIndex = content.indexOf('</TASKS>');
            }
            
            if (tasksEndIndex !== -1) {
                const afterTasks = content.substring(tasksEndIndex + 11);
                const analysisMatch = afterTasks.match(/<ANALYSIS_START>([\s\S]*?)(?:<ANALYSIS_END>|<SQL_START>|<CHART_DATA>|$)/);
                
                if (analysisMatch) {
                    const analysisText = analysisMatch[1].trim();
                    const taskPattern = /任务(\d+)[：:]([\s\S]*?)(?=任务\d+[：:]|$)/g;
                    let match;
                    
                    while ((match = taskPattern.exec(analysisText)) !== null) {
                        const taskId = match[1];
                        const taskDetail = match[2].trim();
                        taskDetailsMap[taskId] = taskDetail;
                    }
                }
            }
            
            taskLines.forEach(line => {
                // 使用增强的任务ID和描述提取
                let match = line.match(/^(\d+)[\s]*[\.。]\s*(.+)$/);
                if (!match) match = line.match(/^(\d+)[\s]*[\)）]\s*(.+)$/);
                if (!match) match = line.match(/^[\(（](\d+)[\)）]\s*(.+)$/);
                
                if (match) {
                    const taskId = match[1];
                    const taskDesc = match[2].trim();
                    const taskDetail = taskDetailsMap[taskId] || '';
                    
                    const li = document.createElement('li');
                    li.className = 'task-item expandable completed';
                    li.dataset.taskId = taskId;
                    li.innerHTML = `
                        <div class="task-header" style="cursor: pointer;">
                            <span class="task-icon">✅</span>
                            <span class="task-number">${taskId}.</span>
                            <span class="task-description">${taskDesc}</span>
                            <span class="expand-icon">▼</span>
                        </div>
                        <div class="task-detail" style="display: none;">${taskDetail}</div>
                    `;
                    
                    // 绑定展开/折叠事件
                    const taskHeader = li.querySelector('.task-header');
                    taskHeader.addEventListener('click', (e) => {
                        e.stopPropagation();
                        const detail = li.querySelector('.task-detail');
                        const icon = li.querySelector('.expand-icon');
                        if (detail.style.display === 'none') {
                            detail.style.display = 'block';
                            icon.textContent = '▲';
                            li.classList.add('expanded');
                        } else {
                            detail.style.display = 'none';
                            icon.textContent = '▼';
                            li.classList.remove('expanded');
                }
                    });
                    
                    taskList.appendChild(li);
                }
            });
            
            tasksDiv.appendChild(taskList);
            container.appendChild(tasksDiv);
            }
        }
        
    // 3. 解析并显示关键发现和总结（从CHART_DATA中）
        let chartData = null;
    const chartDataMatch = content.match(/<CHART_DATA>(.*?)<\/CHART_DATA>/s);
        
    if (chartDataMatch) {
            try {
            chartData = window.safeJSONParse(chartDataMatch[1]);
            } catch (e) {
                console.error('解析CHART_DATA失败:', e);
            }
        } else {
        // 尝试解析纯JSON
        const chartTypeIndex = content.indexOf('"chartType"');
        if (chartTypeIndex !== -1) {
            chartData = extractJsonFromContent(content, chartTypeIndex);
            }
        }
        
    // 显示关键发现
    if (chartData && chartData.keyFindings && chartData.keyFindings.length > 0) {
        const findingsDiv = document.createElement('div');
        findingsDiv.className = 'findings-container';
        findingsDiv.innerHTML = '<h4>💡 关键发现：</h4>';
            
        const findingsList = document.createElement('ul');
        findingsList.className = 'findings-list';
        
                chartData.keyFindings.forEach(finding => {
                    const li = document.createElement('li');
                    li.className = 'finding-item';
            li.innerHTML = `<span class="finding-icon">💡</span><span class="finding-text">${finding}</span>`;
                    findingsList.appendChild(li);
                });
                
        findingsDiv.appendChild(findingsList);
        container.appendChild(findingsDiv);
        }
    
    // 显示分析总结
    if (chartData && chartData.summary) {
        const summaryDiv = document.createElement('div');
        summaryDiv.className = 'summary-container';
        summaryDiv.innerHTML = `
                        <h4>📄 分析总结：</h4>
            <div class="summary-text">${chartData.summary}</div>
                `;
        container.appendChild(summaryDiv);
            }
        }
        
// 从内容中提取JSON对象（使用括号匹配）
function extractJsonFromContent(content, startSearchIndex) {
    let startIndex = startSearchIndex;
    let braceCount = 0;
    
    // 向前查找 {
    for (let i = startSearchIndex; i >= 0; i--) {
        if (content[i] === '}') {
            braceCount++;
        } else if (content[i] === '{') {
            if (braceCount === 0) {
                startIndex = i;
                break;
            } else {
                braceCount--;
            }
        }
    }
    
    // 向后匹配完整JSON
    braceCount = 0;
    let endIndex = -1;
    for (let i = startIndex; i < content.length; i++) {
        if (content[i] === '{') braceCount++;
        else if (content[i] === '}') {
            braceCount--;
            if (braceCount === 0) {
                endIndex = i + 1;
                break;
            }
            }
    }
    
    if (endIndex !== -1) {
        try {
            const jsonStr = content.substring(startIndex, endIndex);
            return JSON.parse(jsonStr);
                    } catch (e) {
            console.error('JSON解析失败:', e);
            return null;
        }
    }
    
    return null;
}
    
// 更新结果分析区域
function updateResultAnalysisFromHistory(messages) {
    console.log('========== 更新结果分析区域 ==========');
    console.log('消息数量:', messages.length);
    
    const resultContent = document.getElementById('result-content');
    if (!resultContent) {
        console.log('找不到 result-content 元素');
        return;
    }
    
    resultContent.innerHTML = '';
    
    // 【修复问题2】遍历所有包含 CHART_DATA 的 assistant 消息，为每条消息创建结果面板
    // 先收集所有包含 CHART_DATA 的消息
    const analysisMessages = [];
    let titleFromFirstMessage = '';
    
    // 先尝试从第一条 assistant 消息中提取标题（作为默认标题）
    for (let i = 0; i < messages.length; i++) {
        const message = messages[i];
        if (message.role !== 'assistant') continue;
        
        const content = message.content;
        const titleMatch = content.match(/<TITLE_START>(.*?)<TITLE_END>/s) || 
                          content.match(/<TITLE>(.*?)<\/TITLE>/s);
        if (titleMatch) {
            titleFromFirstMessage = titleMatch[1].trim();
            console.log('从第一条 assistant 消息提取标题:', titleFromFirstMessage);
            break;
        }
    }
    
    // 遍历所有消息，找出所有包含 CHART_DATA 的 assistant 消息
    for (let i = 0; i < messages.length; i++) {
        const message = messages[i];
        if (message.role !== 'assistant') continue;
        
        const content = message.content;
        const hasChartData = content.includes('<CHART_DATA>') || content.includes('chartType');
        
        if (hasChartData) {
            console.log(`找到包含 CHART_DATA 的消息，索引: ${i}`);
            analysisMessages.push({
                message: message,
                index: i
            });
        }
    }
    
    if (analysisMessages.length === 0) {
        console.log('未找到包含 CHART_DATA 的分析消息');
        return;
    }
    
    console.log(`找到 ${analysisMessages.length} 条包含 CHART_DATA 的分析消息，开始渲染结果分析区域`);
    
    // 为每条分析消息创建结果面板
    analysisMessages.forEach((analysisItem, panelIndex) => {
        const message = analysisItem.message;
        const content = message.content;
        const index = analysisItem.index;
        
        // 创建结果面板
        const resultPanel = document.createElement('div');
        resultPanel.className = 'analysis-result-panel';
        
        // 如果是多条消息，添加分隔线（第一条消息除外）
        if (panelIndex > 0) {
            const separator = document.createElement('div');
            separator.className = 'result-separator';
            separator.style.cssText = 'margin: 20px 0; padding: 10px 0; border-top: 2px solid #e0e0e0;';
            separator.innerHTML = '<div style="text-align: center; color: #999; font-size: 14px;">新的分析结果</div>';
            resultContent.appendChild(separator);
        }
        
        // 1. 提取标题（优先从当前消息，如果没有则使用从第一条消息提取的标题，并添加序号）
        const titleMatch = content.match(/<TITLE_START>(.*?)<TITLE_END>/s) || 
                          content.match(/<TITLE>(.*?)<\/TITLE>/s);
        let title = '';
        if (titleMatch) {
            title = titleMatch[1].trim();
            console.log(`  消息${panelIndex + 1}：从当前消息提取标题:`, title);
        } else if (titleFromFirstMessage) {
            // 如果有多条消息，在标题后添加序号
            title = analysisMessages.length > 1 
                ? `${titleFromFirstMessage} (${panelIndex + 1})`
                : titleFromFirstMessage;
            console.log(`  消息${panelIndex + 1}：使用第一条消息的标题:`, title);
        }
    
        if (title) {
            const headerDiv = document.createElement('div');
            headerDiv.className = 'result-header';
            headerDiv.innerHTML = `<h2 class="result-title">📊 ${title}</h2>`;
            resultPanel.appendChild(headerDiv);
            console.log(`  消息${panelIndex + 1}：添加标题:`, title);
        }
        
        // 2. 解析图表数据
        let chartData = null;
        const chartDataMatch = content.match(/<CHART_DATA>(.*?)<\/CHART_DATA>/s);
        
        if (chartDataMatch) {
            try {
                const jsonStr = chartDataMatch[1].trim();
                console.log(`  消息${panelIndex + 1}：CHART_DATA内容（前200字符）:`, jsonStr.substring(0, 200));
                chartData = window.safeJSONParse(jsonStr);
                if (chartData) {
                    console.log(`  消息${panelIndex + 1}：解析CHART_DATA成功`);
                } else {
                    console.error(`  消息${panelIndex + 1}：解析CHART_DATA失败（返回null）`);
                }
                console.log('  chartData 结构:', Object.keys(chartData));
                console.log('  chartData.data 存在:', !!chartData.data);
                console.log('  chartData.data 内容:', chartData.data);
                
                // 检查 data 字段
                if (chartData.data) {
                    console.log('  data.columns:', chartData.data.columns);
                    console.log('  data.rows:', chartData.data.rows ? chartData.data.rows.length + ' rows' : 'undefined');
                }
            } catch (e) {
                console.error(`  消息${panelIndex + 1}：解析CHART_DATA失败:`, e);
                console.error('  原始内容:', chartDataMatch[1].substring(0, 500));
            }
        } else {
            // 尝试解析纯JSON
            const chartTypeIndex = content.indexOf('"chartType"');
            if (chartTypeIndex !== -1) {
                chartData = extractJsonFromContent(content, chartTypeIndex);
                if (chartData) {
                    console.log(`  消息${panelIndex + 1}：解析纯JSON成功, 结构:`, Object.keys(chartData));
                }
            }
        }
    
        // 3. 显示分析总结
        if (chartData && chartData.summary) {
            const summarySection = document.createElement('div');
            summarySection.className = 'result-section summary-section';
            summarySection.innerHTML = `
                <h3 class="section-title">📄 分析总结</h3>
                <div class="summary-text">${chartData.summary}</div>
            `;
            resultPanel.appendChild(summarySection);
            console.log(`  消息${panelIndex + 1}：添加分析总结`);
        }
        
        // 4. 显示图表区域
        if (chartData && chartData.chartType) {
            console.log(`  消息${panelIndex + 1}：准备显示图表区域`);
            console.log('  chartData 完整对象:', chartData);
            console.log('  chartType:', chartData.chartType);
            console.log('  chartData.data:', chartData.data);
            console.log('  chartData 的所有键:', Object.keys(chartData));
            
            // 详细检查 data 字段
            if (chartData.data) {
                console.log('  ✓ chartData.data 存在');
                console.log('  data 类型:', typeof chartData.data);
                console.log('  data.columns:', chartData.data.columns);
                console.log('  data.rows:', chartData.data.rows);
            } else {
                console.error('  ✗ chartData.data 不存在！');
                console.error('  chartData 内容:', JSON.stringify(chartData).substring(0, 500));
            }
            
            const chartSection = document.createElement('div');
            chartSection.className = 'result-section chart-section';
            chartSection.innerHTML = '<h3 class="section-title">📈 数据可视化</h3>';
            
            const chartContainer = document.createElement('div');
            chartContainer.className = 'chart-result-container';
            const chartId = 'result-chart-' + Date.now() + '-' + index;
            chartContainer.id = chartId;
            chartContainer.style.cssText = 'min-width: 600px; width: auto; min-height: 500px; background: white; overflow: visible; box-sizing: border-box;';
            
            // 检查是否有实际数据
            const hasData = chartData.data && (chartData.data.columns || chartData.data.rows);
            console.log('  hasData 检查结果:', hasData);
            console.log('  检查条件: chartData.data=', !!chartData.data, ', columns=', !!(chartData.data && chartData.data.columns), ', rows=', !!(chartData.data && chartData.data.rows));
            
            chartSection.appendChild(chartContainer);
            resultPanel.appendChild(chartSection);
                
        if (hasData) {
            console.log(`  消息${panelIndex + 1}：准备渲染图表`);
            console.log('  chartId:', chartId);
            console.log('  chartType:', chartData.chartType);
            console.log('  columns:', chartData.data.columns);
            console.log('  rows数量:', chartData.data.rows ? chartData.data.rows.length : 0);
            console.log('  rows示例（前2条）:', chartData.data.rows ? chartData.data.rows.slice(0, 2) : []);
            
            // 先添加到DOM
            resultContent.appendChild(resultPanel);
            console.log(`  消息${panelIndex + 1}：结果面板已添加到DOM`);
        
            // 延迟渲染，确保DOM完全挂载
            setTimeout(() => {
                console.log(`  消息${panelIndex + 1}：开始渲染图表（500ms后）`);
                const container = document.getElementById(chartId);
                console.log('  查找容器 ID:', chartId);
                console.log('  容器找到:', !!container);
                
                if (container) {
                    console.log('  容器宽度:', container.offsetWidth);
                    console.log('  容器高度:', container.offsetHeight);
                }
                
                console.log('  echarts 存在:', typeof echarts !== 'undefined');
                console.log('  renderChart 存在:', typeof window.renderChart === 'function');
                    
                if (container && typeof window.renderChart === 'function') {
                    console.log('  ✓ 开始调用 renderChart');
                    console.log('  传递参数:', {
                        chartType: chartData.chartType,
                        dataColumns: chartData.data.columns,
                        dataRowsCount: chartData.data.rows.length
                    });
                    
                    try {
                        // 【修复】在渲染前确保容器有正确的尺寸
                        if (container.offsetWidth === 0 || container.offsetHeight === 0) {
                            console.log('  容器尺寸为0，等待容器渲染...');
                            setTimeout(() => {
                                window.renderChart(chartData, chartId);
                                console.log('  ✓✓✓ 图表渲染完成！');
                                // 渲染后再次延迟调用resize
                                setTimeout(() => {
                                    if (typeof echarts !== 'undefined') {
                                        const chartInstances = echarts.getInstanceByDom(container);
                                        if (chartInstances && chartInstances.length > 0) {
                                            chartInstances.forEach(chart => {
                                                chart.resize();
                                                console.log('  ✓ 图表resize完成');
                                            });
                                        } else {
                                            window.dispatchEvent(new Event('resize'));
                                            console.log('  ✓ 触发窗口resize事件');
                                        }
                                    }
                                }, 300);
                            }, 200);
                        } else {
                            window.renderChart(chartData, chartId);
                            console.log('  ✓✓✓ 图表渲染完成！');
                            
                            // 【修复问题1】渲染后再次延迟，确保容器尺寸已计算，然后触发resize
                            setTimeout(() => {
                                // 查找所有ECharts实例并调用resize
                                if (typeof echarts !== 'undefined') {
                                    const chartInstances = echarts.getInstanceByDom(container);
                                    if (chartInstances && chartInstances.length > 0) {
                                        chartInstances.forEach(chart => {
                                            chart.resize();
                                            console.log('  ✓ 图表resize完成');
                                        });
                                    } else {
                                        // 如果找不到实例，尝试触发窗口resize事件
                                        window.dispatchEvent(new Event('resize'));
                                        console.log('  ✓ 触发窗口resize事件');
                                    }
                                }
                            }, 300);
                        }
                    } catch (e) {
                        console.error('  ✗✗✗ 图表渲染失败:', e);
                    }
                } else {
                    console.error('  ✗ 无法渲染图表:');
                    console.error('    - 容器存在:', !!container);
                    console.error('    - renderChart函数存在:', typeof window.renderChart === 'function');
                }
            }, 500);
        } else {
            // 没有数据，显示提示
            console.log(`  消息${panelIndex + 1}：没有图表数据，显示占位符`);
            chartContainer.innerHTML = `
                <div style="display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 40px; text-align: center; background: #fff8dc; border: 2px dashed #ffa500; border-radius: 8px; min-height: 400px;">
                    <div style="font-size: 48px; margin-bottom: 16px;">⚠️</div>
                    <div style="font-size: 18px; color: #d97706; font-weight: bold; margin-bottom: 12px;">旧版本历史记录</div>
                    <div style="font-size: 16px; color: #6b7280; margin-bottom: 8px;">图表类型：${chartData.chartType}</div>
                    <div style="font-size: 14px; color: #9ca3af; line-height: 1.6;">
                        此会话创建于后端升级前，历史记录中不包含图表数据。<br>
                        请创建新会话以查看完整的数据可视化图表。
                    </div>
                </div>
            `;
            // 没有数据时也要添加到DOM
            resultContent.appendChild(resultPanel);
            console.log(`  消息${panelIndex + 1}：结果面板已添加到DOM（无数据）`);
        }
        } // 关闭 if (chartData && chartData.chartType) 块
        
        // 如果还没有添加到DOM（在hasData为false的情况下已经添加了），则添加
        if (!resultContent.contains(resultPanel)) {
            resultContent.appendChild(resultPanel);
        }
        
        console.log(`消息${panelIndex + 1}：结果面板渲染完成`);
    });
    
    console.log('========== 所有结果分析面板更新完成 ==========');
}

// 更新数据库页面的结果分析区域（用于历史消息）
function updateDBResultAnalysisFromHistory(messages) {
    console.log('========== 更新数据库结果分析区域（历史消息） ==========');
    console.log('消息数量:', messages.length);
    
    const dbResultContent = document.getElementById('db-result-content');
    if (!dbResultContent) {
        console.log('找不到 db-result-content 元素');
        return;
    }
    
    dbResultContent.innerHTML = '';
    
    // 遍历所有包含 CHART_DATA 的 assistant 消息，为每条消息创建结果面板
    const analysisMessages = [];
    let titleFromFirstMessage = '';
    
    // 先尝试从第一条 assistant 消息中提取标题
    for (let i = 0; i < messages.length; i++) {
        const message = messages[i];
        if (message.role !== 'assistant') continue;
        
        const content = message.content;
        const titleMatch = content.match(/<TITLE_START>(.*?)<TITLE_END>/s) || 
                          content.match(/<TITLE>(.*?)<\/TITLE>/s);
        if (titleMatch) {
            titleFromFirstMessage = titleMatch[1].trim();
            console.log('从第一条 assistant 消息提取标题:', titleFromFirstMessage);
            break;
        }
    }
    
    // 遍历所有消息，找出所有包含 CHART_DATA 的 assistant 消息
    for (let i = 0; i < messages.length; i++) {
        const message = messages[i];
        if (message.role !== 'assistant') continue;
        
        const content = message.content;
        const hasChartData = content.includes('<CHART_DATA>') || content.includes('chartType');
        
        if (hasChartData) {
            console.log(`找到包含 CHART_DATA 的消息，索引: ${i}`);
            analysisMessages.push({
                message: message,
                index: i
            });
        }
    }
    
    if (analysisMessages.length === 0) {
        console.log('未找到包含 CHART_DATA 的分析消息');
        return;
    }
    
    console.log(`找到 ${analysisMessages.length} 条包含 CHART_DATA 的分析消息，开始渲染结果分析区域`);
    
    // 为每条分析消息创建结果面板
    analysisMessages.forEach((analysisItem, panelIndex) => {
        const message = analysisItem.message;
        const content = message.content;
        const index = analysisItem.index;
        
        // 创建结果面板
        const resultPanel = document.createElement('div');
        resultPanel.className = 'analysis-result-panel';
        
        // 如果是多条消息，添加分隔线（第一条消息除外）
        if (panelIndex > 0) {
            const separator = document.createElement('div');
            separator.className = 'result-separator';
            separator.style.cssText = 'margin: 20px 0; padding: 10px 0; border-top: 2px solid #e0e0e0;';
            separator.innerHTML = '<div style="text-align: center; color: #999; font-size: 14px;">新的分析结果</div>';
            dbResultContent.appendChild(separator);
        }
        
        // 1. 提取标题
        const titleMatch = content.match(/<TITLE_START>(.*?)<TITLE_END>/s) || 
                          content.match(/<TITLE>(.*?)<\/TITLE>/s);
        let title = '';
        if (titleMatch) {
            title = titleMatch[1].trim();
        } else if (titleFromFirstMessage) {
            title = analysisMessages.length > 1 
                ? `${titleFromFirstMessage} (${panelIndex + 1})`
                : titleFromFirstMessage;
        }
    
        if (title) {
            const headerDiv = document.createElement('div');
            headerDiv.className = 'result-header';
            headerDiv.innerHTML = `<h2 class="result-title">📊 ${title}</h2>`;
            resultPanel.appendChild(headerDiv);
        }
        
        // 2. 解析图表数据
        let chartData = null;
        const chartDataMatch = content.match(/<CHART_DATA>(.*?)<\/CHART_DATA>/s);
        
        if (chartDataMatch) {
            try {
                const jsonStr = chartDataMatch[1].trim();
                chartData = window.safeJSONParse(jsonStr);
            } catch (e) {
                console.error(`解析CHART_DATA失败:`, e);
            }
        } else {
            const chartTypeIndex = content.indexOf('"chartType"');
            if (chartTypeIndex !== -1) {
                chartData = extractJsonFromContent(content, chartTypeIndex);
            }
        }
    
        // 3. 显示分析总结
        if (chartData && chartData.summary) {
            const summarySection = document.createElement('div');
            summarySection.className = 'result-section summary-section';
            summarySection.innerHTML = `
                <h3 class="section-title">📄 分析总结</h3>
                <div class="summary-text">${chartData.summary}</div>
            `;
            resultPanel.appendChild(summarySection);
        }
        
        // 4. 显示图表区域
        if (chartData && chartData.chartType) {
            const chartSection = document.createElement('div');
            chartSection.className = 'result-section chart-section';
            chartSection.innerHTML = '<h3 class="section-title">📈 数据可视化</h3>';
            
            const chartContainer = document.createElement('div');
            chartContainer.className = 'chart-result-container';
            const chartId = 'db-result-chart-' + Date.now() + '-' + index;
            chartContainer.id = chartId;
            chartContainer.style.cssText = 'min-width: 600px; width: auto; min-height: 500px; background: white; overflow: visible; box-sizing: border-box;';
            
            const hasData = chartData.data && (chartData.data.columns || chartData.data.rows);
            
            chartSection.appendChild(chartContainer);
            resultPanel.appendChild(chartSection);
            
            if (hasData) {
                // 延迟渲染，确保DOM完全挂载
                setTimeout(() => {
                    const container = document.getElementById(chartId);
                    if (container && typeof window.renderChart === 'function') {
                        try {
                            window.renderChart(chartData, chartId);
                            console.log(`数据库消息${panelIndex + 1}：图表渲染完成`);
                            
                            setTimeout(() => {
                                if (typeof echarts !== 'undefined') {
                                    const chartInstances = echarts.getInstanceByDom(container);
                                    if (chartInstances && chartInstances.length > 0) {
                                        chartInstances.forEach(chart => chart.resize());
                                    } else {
                                        window.dispatchEvent(new Event('resize'));
                                    }
                                }
                            }, 300);
                        } catch (e) {
                            console.error('数据库图表渲染失败:', e);
                        }
                    }
                }, 500);
                
                dbResultContent.appendChild(resultPanel);
            } else {
                chartContainer.innerHTML = `
                    <div style="display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 40px; text-align: center; background: #fff8dc; border: 2px dashed #ffa500; border-radius: 8px; min-height: 400px;">
                        <div style="font-size: 48px; margin-bottom: 16px;">⚠️</div>
                        <div style="font-size: 18px; color: #d97706; font-weight: bold; margin-bottom: 12px;">旧版本历史记录</div>
                        <div style="font-size: 16px; color: #6b7280; margin-bottom: 8px;">图表类型：${chartData.chartType}</div>
                        <div style="font-size: 14px; color: #9ca3af; line-height: 1.6;">
                            此会话创建于后端升级前，历史记录中不包含图表数据。<br>
                            请创建新会话以查看完整的数据可视化图表。
                        </div>
                    </div>
                `;
                dbResultContent.appendChild(resultPanel);
            }
        } else {
            if (!chartData || !chartData.chartType) {
                dbResultContent.appendChild(resultPanel);
            }
        }
        
        // 如果还没有添加到DOM
        if (!dbResultContent.contains(resultPanel)) {
            dbResultContent.appendChild(resultPanel);
        }
    });
    
    console.log('========== 数据库结果分析面板更新完成 ==========');
}

// 更新数据库对话页面的结果分析区域
function updateDatabaseResultAnalysis(content, chartData) {
    console.log('========== 更新数据库结果分析区域 ==========');
    console.log('content长度:', content.length);
    console.log('chartData:', chartData);
    
    const dbResultContent = document.getElementById('db-result-content');
    if (!dbResultContent) {
        console.log('找不到 db-result-content 元素');
        return;
    }
    
    // 如果没有直接传入图表数据，尝试从内容中提取
    if (!chartData) {
        const chartDataMatch = content.match(/<CHART_DATA>([\s\S]*?)<\/CHART_DATA>/);
        if (chartDataMatch) {
            try {
                const chartDataJson = chartDataMatch[1];
                chartData = window.safeJSONParse(chartDataJson);
                if (chartData) {
                    console.log('从内容中提取到数据库图表数据:', chartData);
                } else {
                    console.error('解析数据库图表数据失败（返回null）');
                }
            } catch (e) {
                console.error('解析数据库图表数据失败:', e);
            }
        }
    }
    
    // 【修复】如果已经有内容，添加分割线（追加模式，不再清空现有内容）
    if (dbResultContent.children.length > 0) {
        const separator = document.createElement('div');
        separator.className = 'result-separator';
        separator.innerHTML = '<hr><div class="separator-text">新的分析结果</div><hr>';
        dbResultContent.appendChild(separator);
        console.log('添加分割线（追加模式）');
    }
    
    // 创建结果面板
    const resultPanel = document.createElement('div');
    resultPanel.className = 'analysis-result-panel';
    
    // 1. 显示标题（优先使用chartData中的title，其次从content中提取）
    let title = '';
    if (chartData && chartData.title) {
        title = chartData.title;
        console.log('从chartData获取标题:', title);
    } else {
        // 从content中提取标题
        const titleMatch = content.match(/<TITLE_START>(.*?)<TITLE_END>/s) || 
                          content.match(/<TITLE>(.*?)<\/TITLE>/s);
        if (titleMatch) {
            title = titleMatch[1].trim();
            console.log('从内容提取标题:', title);
        }
    }
    
    if (title) {
        const headerDiv = document.createElement('div');
        headerDiv.className = 'result-header';
        // Unicode解码标题（如果decodeUnicodeString函数存在）
        let decodedTitle = title;
        if (typeof decodeUnicodeString === 'function') {
            decodedTitle = decodeUnicodeString(title);
        }
        // HTML转义
        const escapedTitle = decodedTitle
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');
        headerDiv.innerHTML = `<h2 class="result-title">📊 ${escapedTitle}</h2>`;
        resultPanel.appendChild(headerDiv);
        console.log('添加标题:', decodedTitle);
    }
    
    // 2. 显示分析总结
    if (chartData && chartData.summary) {
        const summarySection = document.createElement('div');
        summarySection.className = 'result-section summary-section';
        // Unicode解码总结（如果decodeUnicodeString函数存在）
        let decodedSummary = chartData.summary;
        if (typeof decodeUnicodeString === 'function') {
            decodedSummary = decodeUnicodeString(chartData.summary);
        }
        // HTML转义
        const escapedSummary = decodedSummary
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/\n/g, '<br>');
        summarySection.innerHTML = `
            <h3 class="section-title">📝 分析总结</h3>
            <p class="summary-text">${escapedSummary}</p>
        `;
        resultPanel.appendChild(summarySection);
        console.log('添加分析总结');
    }
    
    // 3. 显示图表区域
    if (chartData && chartData.data) {
        console.log('准备显示数据库图表区域');
        console.log('  chartType:', chartData.chartType);
        console.log('  chartData.data:', chartData.data);
        
        const chartSection = document.createElement('div');
        chartSection.className = 'result-section chart-section';
        chartSection.innerHTML = '<h3 class="section-title">📈 数据可视化</h3>';
        
        const chartContainer = document.createElement('div');
        chartContainer.className = 'chart-result-container';
        const chartId = 'db-result-chart-' + Date.now();
        chartContainer.id = chartId;
        chartContainer.style.cssText = 'min-width: 600px; width: auto; min-height: 400px; background: white;';
        
        // 检查是否有实际数据
        const hasData = chartData.data && (chartData.data.columns || chartData.data.rows);
        console.log('  hasData 检查结果:', hasData);
        
        chartSection.appendChild(chartContainer);
        resultPanel.appendChild(chartSection);
        
        // 先添加到DOM（无论是否有数据）
        dbResultContent.appendChild(resultPanel);
        console.log('数据库结果面板已添加到DOM');
        
        if (hasData) {
            console.log('准备渲染数据库图表');
            console.log('  chartId:', chartId);
            console.log('  chartType:', chartData.chartType);
            console.log('  columns:', chartData.data.columns);
            console.log('  rows数量:', chartData.data.rows ? chartData.data.rows.length : 0);
            
            // 延迟渲染，确保DOM完全挂载
            setTimeout(() => {
                console.log('开始渲染数据库图表（100ms后）');
                const container = document.getElementById(chartId);
                console.log('  查找容器 ID:', chartId);
                console.log('  容器找到:', !!container);
                
                if (container && typeof window.renderChart === 'function') {
                    console.log('  ✓ 开始调用 renderChart');
                    try {
                        window.renderChart(chartData, chartId);
                        console.log('  ✓✓✓ 数据库图表渲染完成！');
                    } catch (e) {
                        console.error('  ✗✗✗ 数据库图表渲染失败:', e);
                    }
                } else {
                    console.error('  ✗ 无法渲染数据库图表:');
                    console.error('    - 容器存在:', !!container);
                    console.error('    - renderChart函数存在:', typeof window.renderChart === 'function');
                }
            }, 100);
        } else {
            // 没有数据，显示提示
            console.log('没有数据库图表数据，显示占位符');
            chartContainer.innerHTML = `
                <div style="display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 40px; text-align: center; background: #fff8dc; border: 2px dashed #ffa500; border-radius: 8px; min-height: 400px;">
                    <div style="font-size: 48px; margin-bottom: 16px;">⚠️</div>
                    <div style="font-size: 18px; color: #d97706; font-weight: bold; margin-bottom: 12px;">无图表数据</div>
                    <div style="font-size: 14px; color: #9ca3af; line-height: 1.6;">
                        当前结果不包含图表数据。
                    </div>
                </div>
            `;
        }
    } else {
        // 没有图表数据，但如果有标题或总结，也要添加到DOM
        if (resultPanel.children.length > 0) {
            dbResultContent.appendChild(resultPanel);
            console.log('数据库结果面板已添加到DOM（无图表数据）');
        } else {
            console.log('没有可显示的内容（无标题、总结和图表）');
        }
    }
    
    console.log('========== 数据库结果分析区域更新完成 ==========');
}

// 清空结果分析和消息显示区域的脏数据
function clearResultAnalysisAndMessages() {
    // 清空结果分析区域
    const resultContent = document.getElementById('result-content');
    if (resultContent) {
        resultContent.innerHTML = '';
        console.log('已清空结果分析区域');
    }
    
    // 清空消息显示区域
    const chatMessages = document.getElementById('chat-messages');
    if (chatMessages) {
        chatMessages.innerHTML = '';
        console.log('已清空消息显示区域');
    }
    
    // 清空消息状态
    APP_STATE.messages = [];
    APP_STATE.currentSession = null;
    
    // 移除会话文件信息显示（如果存在）
    const sessionFileInfo = document.getElementById('session-file-info');
    if (sessionFileInfo) {
        sessionFileInfo.remove();
    }
}

// 清空文件预览区域的脏数据
function clearFilePreview() {
    // 隐藏Excel预览，显示上传提示
    const uploadPrompt = document.getElementById('upload-prompt');
    const excelPreview = document.getElementById('excel-preview');
    const excelTable = document.getElementById('excel-table');
    const sheetTabs = document.getElementById('sheet-tabs');
    const excelFileName = document.getElementById('excel-file-name');
    
    if (uploadPrompt) {
        uploadPrompt.style.display = '';
    }
    if (excelPreview) {
        excelPreview.style.display = 'none';
    }
    
    // 清空表格内容
    if (excelTable) {
        excelTable.innerHTML = '';
    }
    
    // 清空工作表标签
    if (sheetTabs) {
        sheetTabs.innerHTML = '';
    }
    
    // 清空文件名
    if (excelFileName) {
        excelFileName.textContent = '';
    }
    
    // 清空文件状态
    APP_STATE.currentFile = null;
    
    // 移除关联的历史消息显示（如果存在）
    const fileRelatedHistory = document.getElementById('file-related-history');
    if (fileRelatedHistory) {
        fileRelatedHistory.remove();
    }
    
    console.log('已清空文件预览区域');
}

// 预览文件
async function previewFile(fileId) {
    try {
        // 从文件列表中获取文件信息
        const fileInfo = APP_STATE.files.find(f => f.fileId === fileId);
        if (!fileInfo) {
            showMessage('文件信息未找到', 'error');
            return;
        }

        // 根据文件扩展名判断文件类型
        const fileName = fileInfo.fileName || '';
        const fileExt = fileName.toLowerCase().split('.').pop();

        // SQLite文件扩展名
        const sqliteExtensions = ['db', 'sqlite', 'sqlite3'];

        if (sqliteExtensions.includes(fileExt)) {
            // SQLite文件：连接数据库并切换到DB助手页面
            console.log('预览SQLite文件:', fileName);

            const connectionInfo = { type: 'SQLite', fileId, fileName, filePath: `本地文件: ${fileName}` };
            const connectionData = { type: 'SQLite', SQLite: { fileId, readonly: false } };

            if (typeof showLoading === 'function') {
                showLoading('正在连接数据库...');
            }

            try {
                const connectResponse = await connectDatabase(connectionData);

                if (connectResponse && connectResponse.success && connectResponse.result) {
                    const connectionId = connectResponse.result.connectionId;
                    console.log('数据库连接成功，connectionId:', connectionId);

                    const tablesRes = await getTableList(connectionId);
                    const tables = (tablesRes && tablesRes.success && tablesRes.result && tablesRes.result.tables) ? tablesRes.result.tables : [];
                    let tableSchema = null;
                    let tableData = [];
                    if (tables.length > 0) {
                        const firstTableRes = await getTableData(connectionId, tables[0]);
                        if (firstTableRes && firstTableRes.success && firstTableRes.result && firstTableRes.result.tableSchema) {
                            tableSchema = { ...firstTableRes.result.tableSchema, tableName: tables[0] };
                            tableData = firstTableRes.result.tableSchema.tableData || [];
                        } else {
                            tableSchema = { tableName: tables[0], columnInfo: [] };
                        }
                    }

                    if (!APP_STATE.databases) {
                        APP_STATE.databases = {};
                    }
                    if (!APP_STATE.databases.sqlite) {
                        APP_STATE.databases.sqlite = {};
                    }
                    APP_STATE.databases.sqlite.connectionId = connectionId;
                    APP_STATE.databases.sqlite.connectionInfo = connectionInfo;
                    APP_STATE.databases.sqlite.dbType = 'SQLite';
                    APP_STATE.databases.sqlite.tables = tables;
                    APP_STATE.databases.sqlite.tableSchema = tableSchema;
                    APP_STATE.databases.sqlite.currentTable = tables[0] || null;
                    APP_STATE.databases.current = APP_STATE.databases.sqlite;
                    
                    APP_STATE.databases.tablePagination = {
                        currentPage: 1,
                        totalPages: 1,
                        totalRows: 0,
                        pageSize: 50,
                        currentTableName: null
                    };

                    document.dispatchEvent(new CustomEvent('databaseConnected', {
                        detail: {
                            connectionId: APP_STATE.databases.sqlite.connectionId,
                            connectionInfo: APP_STATE.databases.sqlite.connectionInfo,
                            tables: APP_STATE.databases.sqlite.tables,
                            tableSchema: APP_STATE.databases.sqlite.tableSchema
                        }
                    }));

                    const databasePage = document.getElementById('database-page');
                    if (databasePage && databasePage.classList.contains('active')) {
                        updateConnectedDatabaseDisplay();
                    }

                    switchToDatabaseChatPage({
                        connectionId,
                        tables,
                        tableSchema,
                        tableData
                    });

                    // 更新导航栏选中状态到智能DB助手
                    const navItems = document.querySelectorAll('.nav-item');
                    navItems.forEach(nav => nav.classList.remove('active'));
                    const dbNav = document.querySelector('.nav-item[data-page="database"]');
                    if (dbNav) {
                        dbNav.classList.add('active');
                    }

                    if (typeof showMessage === 'function') {
                        showMessage('数据库连接成功，现在可以对数据库进行分析', 'success');
                    } else {
                        alert('数据库连接成功，现在可以对数据库进行分析');
                    }
                } else {
                    const errorMsg = connectResponse?.message || connectResponse?.errorMsg || '数据库连接失败';
                    console.error('数据库连接失败:', errorMsg);
                    if (typeof showMessage === 'function') {
                        showMessage(errorMsg, 'error');
                    } else {
                        alert(errorMsg);
                    }
                }
            } catch (connectError) {
                console.error('数据库连接异常:', connectError);
                const errorMsg = connectError.message || '数据库连接过程中发生错误';
                if (typeof showMessage === 'function') {
                    showMessage(errorMsg, 'error');
                } else {
                    alert(errorMsg);
                }
            } finally {
                if (typeof hideLoading === 'function') {
                    hideLoading();
                }
            }
        } else {
            // Excel文件或其他文件：使用原有的Excel预览逻辑
            console.log('预览Excel文件:', fileName);
            const response = await FileAPI.previewExcel(fileId);
            if (response.success) {
                displayExcelPreview(response.result);

                APP_STATE.currentFile = {
                    id: fileId,
                    name: fileInfo.fileName,
                    size: fileInfo.fileSize,
                    data: response.result
                };

                // 清空可能存在的关联历史消息显示（若旧版本曾创建）
                const historyContainer = document.getElementById('file-related-history');
                if (historyContainer) {
                    historyContainer.remove();
                }

                // 切换到模型对话页面
                switchPage('chat');

                // 切换到文件预览标签页
                switchTab('file-preview');

                showMessage('文件预览成功，现在可以对该文件进行分析', 'success');
            } else {
                console.error('预览文件失败:', response.message);
                showMessage('预览文件失败: ' + response.message, 'error');
            }
        }
    } catch (error) {
        console.error('预览文件失败:', error);
        showMessage('预览文件失败', 'error');
    }
}

// 显示会话关联的文件信息
function displaySessionFileInfo(fileInfo) {
    // 在聊天消息区域顶部添加文件信息显示
    const chatMessages = document.getElementById('chat-messages');
    if (!chatMessages) return;
    
    // 检查是否已经显示过文件信息
    let fileInfoDiv = document.getElementById('session-file-info');
    if (fileInfoDiv) {
        fileInfoDiv.remove();
    }
    
    fileInfoDiv = document.createElement('div');
    fileInfoDiv.id = 'session-file-info';
    fileInfoDiv.className = 'session-file-info';
    fileInfoDiv.innerHTML = `
        <div class="file-info-header">
            <span class="file-icon">📄</span>
            <span class="file-name">${fileInfo.fileName}</span>
            <span class="file-size">(${(fileInfo.fileSize / 1024).toFixed(2)} KB)</span>
            <button class="btn-preview-file" onclick="previewFile('${fileInfo.fileId}')">预览文件</button>
        </div>
    `;
    
    // 插入到消息列表的最前面
    chatMessages.insertBefore(fileInfoDiv, chatMessages.firstChild);
}

// 切换到关联的会话（供「查看会话」按钮等全局调用）
async function switchToRelatedSession(chatSessionId) {
    const session = APP_STATE.sessions.find(s => s.id === chatSessionId);
    if (session) {
        await switchToSession(session);
    } else {
        try {
            const historyResponse = await ModelAPI.getSessionHistory(chatSessionId);
            if (historyResponse.success) {
                let sessionFileId = null;
                if (historyResponse.fileId && historyResponse.fileInfo) {
                    sessionFileId = historyResponse.fileId;
                }
                const messages = (historyResponse.result && historyResponse.result.messageList) || [];
                APP_STATE.currentSession = {
                    id: chatSessionId,
                    title: '未命名会话',
                    created_at: Date.now() / 1000,
                    messages: messages,
                    fileId: sessionFileId
                };
                APP_STATE.messages = APP_STATE.currentSession.messages;
                if (historyResponse.fileId && historyResponse.fileInfo) {
                    APP_STATE.currentFile = {
                        id: historyResponse.fileId,
                        name: historyResponse.fileInfo.fileName,
                        size: historyResponse.fileInfo.fileSize,
                        data: null
                    };
                }
                switchPage('chat');
                setTimeout(() => {
                    updateMessagesDisplay();
                }, 100);
                const sendBtn = document.getElementById('send-btn');
                if (sendBtn) {
                    sendBtn.disabled = false;
                }
            }
        } catch (error) {
            console.error('切换会话失败:', error);
            showMessage('切换会话失败: ' + error.message, 'error');
        }
    }
}

// 将函数暴露到全局作用域
window.switchToRelatedSession = switchToRelatedSession;

// 下载文件
window.downloadFile = async function(fileId) {
    try {
        const response = await FileAPI.downloadFile(fileId);
        if (response.success) {
            // 创建下载链接
            const blob = response.result;
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            
            // 从文件列表中获取文件名
            const file = APP_STATE.files.find(f => f.fileId === fileId);
            a.download = file ? file.fileName : 'download.xlsx';
            
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
            
            showMessage('文件下载成功', 'success');
        }
    } catch (error) {
        console.error('下载文件失败:', error);
        showMessage('下载文件失败', 'error');
    }
}

// 删除文件
async function deleteFile(fileId) {
    if (!confirm('确定要删除这个文件吗？')) {
        return;
    }
    
    try {
        const response = await FileAPI.deleteFile(fileId);
        if (response.success) {
            // 从文件列表中移除
            APP_STATE.files = APP_STATE.files.filter(f => f.fileId !== fileId);
            updateFilesListDisplay();
            
            showMessage('文件删除成功', 'success');
        }
    } catch (error) {
        console.error('删除文件失败:', error);
        showMessage('删除文件失败', 'error');
    }
}

// 删除会话
async function deleteSession(sessionId) {
    if (!confirm('确定要删除这个会话吗？')) {
        return;
    }
    
    try {
        const response = await ModelAPI.deleteSession(sessionId);
        if (response.success) {
            // 【修复】使用后端返回的字段名 id，而不是 sessionId
            APP_STATE.sessions = APP_STATE.sessions.filter(s => s.id !== sessionId);
            updateSessionsListDisplay();
            
            // 如果删除的是当前会话，清空当前会话
            if (APP_STATE.currentSession && APP_STATE.currentSession.id === sessionId) {
                APP_STATE.currentSession = null;
                APP_STATE.messages = [];
                updateMessagesDisplay();
                
                // 禁用发送按钮
                const sendBtn = document.getElementById('send-btn');
                if (sendBtn) {
                    sendBtn.disabled = true;
                }
            }
            
            showMessage('会话删除成功', 'success');
        }
    } catch (error) {
        console.error('删除会话失败:', error);
        showMessage('删除会话失败', 'error');
    }
}

// 初始化页面导航
function initPageNavigation() {
    const navItems = document.querySelectorAll('.nav-item');
    
    navItems.forEach(item => {
        item.addEventListener('click', function() {
            const pageName = this.getAttribute('data-page');
            // 【修复】switchPage 函数内部已经会更新导航栏状态，这里不需要重复更新
            switchPage(pageName);
        });
    });
    
    // 初始化标签页切换
    initTabSwitching();
}

// 切换页面
function switchPage(pageName) {
    // 隐藏所有页面
    const pages = document.querySelectorAll('.page');
    pages.forEach(page => page.classList.remove('active'));

    // 显示目标页面
    const targetPage = document.getElementById(pageName + '-page');
    if (targetPage) {
        targetPage.classList.add('active');

        // 【新增】页面切换时，通过PageSessionManager切换到对应页面的会话
        PageSessionManager.switchSessionByPage(pageName);

        // 【修复】立即更新导航栏激活状态，确保状态及时更新
        const navItems = document.querySelectorAll('.nav-item');
        navItems.forEach(nav => nav.classList.remove('active'));

        const targetNav = document.querySelector(`.nav-item[data-page="${pageName}"]`);
        if (targetNav) {
            targetNav.classList.add('active');
        } else {
            console.warn(`未找到导航项: data-page="${pageName}"`);
        }

        // 触发页面切换事件
        document.dispatchEvent(new CustomEvent('pageChanged', {
            detail: { page: pageName }
        }));
        
        // 使用setTimeout确保DOM更新完成后再刷新列表
        setTimeout(async () => {
            // 如果是文件页面，刷新文件列表
            if (pageName === 'files') {
                try {
                    const filesResponse = await FileAPI.getFileList();
                    console.log('文件列表API响应:', filesResponse);
                    if (filesResponse && filesResponse.success) {
                        // 根据API实际返回的数据结构处理
                        let fileList = [];
                        
                        if (filesResponse.result && Array.isArray(filesResponse.result)) {
                            fileList = filesResponse.result;
                        } else if (filesResponse.data && filesResponse.data.fileList && Array.isArray(filesResponse.data.fileList)) {
                            fileList = filesResponse.data.fileList;
                        } else if (filesResponse.result && typeof filesResponse.result === 'object') {
                            fileList = Object.values(filesResponse.result);
                        }
                        
                        // 标准化文件数据格式
                        APP_STATE.files = fileList.map((file, index) => {
                            if (typeof file === 'string') {
                                return {
                                    fileName: file,
                                    fileId: 'file_' + index,
                                    fileSize: 0
                                };
                            } else if (typeof file === 'object' && file !== null) {
                                return {
                                    fileName: file.fileName || file.name || file.filename || '未知文件',
                                    fileId: file.fileId || file.id || file.file_id || 'file_' + index,
                                    fileSize: file.fileSize || file.size || file.file_size || 0
                                };
                            } else {
                                return {
                                    fileName: '未知文件',
                                    fileId: 'file_' + index,
                                    fileSize: 0
                                };
                            }
                        });
                        
                        updateFilesList();
                    } else {
                        APP_STATE.files = [];
                        updateFilesList();
                    }
                } catch (error) {
                    console.warn('获取文件列表失败:', error);
                    APP_STATE.files = [];
                    updateFilesList();
                }
            }
            // 如果是历史会话页面，刷新会话列表
            else if (pageName === 'history') {
                try {
                    const sessionsResponse = await ModelAPI.getSessions();
                    console.log('switchPage - 获取会话列表响应:', sessionsResponse);
                    if (sessionsResponse && sessionsResponse.success) {
                        const list = (sessionsResponse.result && sessionsResponse.result.chatSessionLists) || [];
                        APP_STATE.sessions = list.map(s => ({
                            id: s.chatSessionId,
                            model: s.modelName,
                            title: s.title,
                            created_at: s.createdAt,
                            updated_at: s.updatedAt,
                            message_count: s.messageCount,
                            first_user_message_content: s.firstUserMessageContent,
                            sessionType: s.sessionType,
                            dbConnectionInfo: s.dbConnectionInfo
                        }));
                        console.log('switchPage - 会话列表数量:', APP_STATE.sessions.length);
                        // 【修复】应该调用 updateSessionsListDisplay 而不是 updateSessionsList
                        updateSessionsListDisplay();
                    }
                } catch (error) {
                    console.warn('获取会话列表失败:', error);
                    APP_STATE.sessions = [];
                    updateSessionsListDisplay();
                }
            }
            // 如果是个人中心页面，加载用户信息和文件列表
            else if (pageName === 'profile') {
                await loadUserInfo();
                await updateProfileFilesList();
            }
            // 如果是连接数据库页面，显示已连接的数据库
            else if (pageName === 'database') {
                updateConnectedDatabaseDisplay();

                // 根据当前连接的数据库类型设置默认选中的标签
                setTimeout(() => {
                    if (APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.dbType) {
                        const dbType = APP_STATE.databases.current.dbType.toLowerCase(); // mysql 或 sqlite
                        const dbTabBtn = document.querySelector(`.db-tab-btn[data-db-type="${dbType}"]`);
                        if (dbTabBtn) {
                            // 移除所有标签的active状态
                            const allTabBtns = document.querySelectorAll('.db-tab-btn');
                            allTabBtns.forEach(btn => btn.classList.remove('active'));

                            // 设置当前标签为active
                            dbTabBtn.classList.add('active');

                            // 切换到对应的表单
                            switchDatabaseTab(dbType);
                        }
                    }
                }, 100); // 稍微延迟执行，确保DOM更新完成
            }
        }, 10);
    }
}

// 初始化标签页切换
function initTabSwitching() {
    // 普通聊天页面的标签页
    const chatTabBtns = document.querySelectorAll('#chat-page .tab-btn');
    chatTabBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const tabName = this.getAttribute('data-tab');
            switchTab(tabName);
            
            // 更新标签页按钮状态
            chatTabBtns.forEach(tab => tab.classList.remove('active'));
            this.classList.add('active');
        });
    });
    
    // 数据库对话页面的标签页
    const dbTabBtns = document.querySelectorAll('#db-chat-page .tab-btn');
    dbTabBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const tabName = this.getAttribute('data-tab');
            // 【修复问题1】使用正确的函数名切换数据库对话页面的标签页
            switchDatabaseChatTab(tabName);
        });
    });
}

// 切换数据库对话页面的标签页（表预览/结果分析）
function switchDatabaseChatTab(tabName) {
    console.log('切换数据库对话页面标签页到:', tabName);
    
    // 【修复问题1】只切换数据库对话页面的标签页，不影响其他页面的标签页
    const dbChatPage = document.getElementById('db-chat-page');
    if (!dbChatPage) {
        console.error('找不到数据库对话页面容器');
        return;
    }
    
    // 隐藏所有标签页内容（只在数据库对话页面内）
    const tabContents = dbChatPage.querySelectorAll('.tab-content');
    tabContents.forEach(content => content.classList.remove('active'));
    
    // 显示目标标签页内容
    let targetTab = null;
    if (tabName === 'table-preview') {
        targetTab = dbChatPage.querySelector('#table-preview');
    } else if (tabName === 'result-analysis') {
        targetTab = dbChatPage.querySelector('#db-result-analysis');
    }
    
    if (targetTab) {
        targetTab.classList.add('active');
        console.log('数据库对话页面标签页切换成功:', tabName);
    } else {
        console.error('找不到数据库对话页面标签页:', tabName);
    }
    
    // 更新标签按钮状态（只在数据库对话页面内）
    const tabBtns = dbChatPage.querySelectorAll('.tab-btn');
    tabBtns.forEach(btn => {
        if (btn.getAttribute('data-tab') === tabName) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
}

// 切换数据库连接类型标签页（MySQL/SQLite）
function switchDatabaseTab(dbType) {
    // 隐藏所有数据库表单
    const dbForms = document.querySelectorAll('.db-form');
    dbForms.forEach(form => form.classList.remove('active'));

    // 显示目标数据库表单
    const targetForm = document.getElementById(dbType + '-form');
    if (targetForm) {
        targetForm.classList.add('active');
        console.log('切换到数据库类型:', dbType);
    } else {
        console.error('找不到数据库表单:', dbType);
    }

    // 【修复】更新数据库tab按钮状态
    const dbTabBtns = document.querySelectorAll('.db-tab-btn');
    dbTabBtns.forEach(btn => {
        if (btn.getAttribute('data-db-type') === dbType) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });

    // 切换当前数据库连接信息
    // 【修复】确保 databases 对象存在
    if (!APP_STATE.databases) {
        APP_STATE.databases = {};
    }
    
    if (APP_STATE.databases[dbType]) {
        APP_STATE.databases.current = APP_STATE.databases[dbType];
        console.log('切换当前数据库为:', dbType);
    } else {
        // 如果该类型的数据库没有连接，清空当前数据库
        APP_STATE.databases.current = null;
        console.log('数据库类型', dbType, '未连接，清空当前数据库');
    }

    // 更新连接状态显示
    updateConnectedDatabaseDisplay();
}

// 切换标签页（暴露为全局函数）
window.switchTab = function(tabName) {
    console.log('切换标签页到:', tabName);
    
    // 隐藏所有标签页内容
    const tabContents = document.querySelectorAll('.tab-content');
    tabContents.forEach(content => content.classList.remove('active'));
    
    // 显示目标标签页内容
    const targetTab = document.getElementById(tabName);
    if (targetTab) {
        targetTab.classList.add('active');
        console.log('标签页切换成功:', tabName);
    } else {
        console.error('找不到标签页:', tabName);
    }
    
    // 更新标签按钮状态
    const tabBtns = document.querySelectorAll('.tab-btn');
    tabBtns.forEach(btn => {
        if (btn.getAttribute('data-tab') === tabName) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
    
    // 更新当前标签状态
    APP_STATE.currentTab = tabName;
};

// 初始化数据库连接页面
function initDatabaseConnection() {
    // 初始化数据库类型tab切换
    const dbTabBtns = document.querySelectorAll('.db-tab-btn');
    dbTabBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const dbType = this.getAttribute('data-db-type');
            switchDatabaseTab(dbType);
            
            // 更新tab按钮状态
            dbTabBtns.forEach(tab => tab.classList.remove('active'));
            this.classList.add('active');
        });
    });
    
    // 初始化MySQL连接表单
    const mysqlForm = document.getElementById('mysql-connection-form');
    if (mysqlForm) {
        mysqlForm.addEventListener('submit', handleMySQLConnect);
    }
    
    // 已删除测试连接按钮，不再需要绑定事件
    
    // 初始化SQLite连接表单
    const sqliteForm = document.getElementById('sqlite-connection-form');
    if (sqliteForm) {
        sqliteForm.addEventListener('submit', handleSQLiteConnect);
    }
    
    // 已删除测试连接按钮，不再需要绑定事件
    
    // 初始化SQLite文件浏览按钮
    const sqliteBrowseBtn = document.getElementById('sqlite-browse-btn');
    if (sqliteBrowseBtn) {
        sqliteBrowseBtn.addEventListener('click', handleSQLiteBrowse);
    }
    
    // 初始化SQLite文件上传按钮
    const sqliteUploadBtn = document.getElementById('sqlite-upload-btn');
    if (sqliteUploadBtn) {
        sqliteUploadBtn.addEventListener('click', handleSQLiteUpload);
    }
    
    // 初始化SQLite文件选择输入框
    const sqliteFileInput = document.getElementById('sqlite-file-input');
    if (sqliteFileInput) {
        sqliteFileInput.addEventListener('change', handleSQLiteFileSelect);
    }
}

// 切换数据库类型tab
// 处理MySQL连接
async function handleMySQLConnect(event) {
    event.preventDefault();
    
    // 收集表单数据
    const host = document.getElementById('mysql-host').value.trim();
    const port = parseInt(document.getElementById('mysql-port').value) || 3306;
    const databaseName = document.getElementById('mysql-database').value.trim();
    const username = document.getElementById('mysql-username').value.trim();
    const password = document.getElementById('mysql-password').value;
    const charset = document.getElementById('mysql-charset').value || 'utf8mb4';
    
    // 验证必填字段
    if (!host || !databaseName || !username || !password) {
        if (typeof showMessage === 'function') {
            showMessage('请填写所有必填字段', 'error');
        } else {
            alert('请填写所有必填字段');
        }
        return;
    }
    
    const connectionData = {
        type: 'MySQL',
        MySQL: {
            host,
            port,
            name: databaseName,
            username,
            password,
            charset
        }
    };
    
    console.log('连接MySQL数据库，参数:', { ...connectionData.MySQL, password: '***' });
    
    try {
        if (typeof showLoading === 'function') {
            showLoading();
        }
        
        const response = await connectDatabase(connectionData);
        
        if (response && response.success && response.result) {
            const connectionId = response.result.connectionId;
            if (typeof showMessage === 'function') {
                showMessage('MySQL数据库连接成功！', 'success');
            } else {
                alert('MySQL数据库连接成功！');
            }
            console.log('MySQL连接成功，connectionId:', connectionId);
            
            // 连上后先获取表列表，再获取第一张表数据，最后切换页面
            const tablesRes = await getTableList(connectionId);
            const tables = (tablesRes && tablesRes.success && tablesRes.result && tablesRes.result.tables) ? tablesRes.result.tables : [];
            let tableSchema = null;
            let tableData = [];
            if (tables.length > 0) {
                const firstTableRes = await getTableData(connectionId, tables[0]);
                if (firstTableRes && firstTableRes.success && firstTableRes.result && firstTableRes.result.tableSchema) {
                    tableSchema = { ...firstTableRes.result.tableSchema, tableName: tables[0] };
                    tableData = firstTableRes.result.tableSchema.tableData || [];
                } else {
                    tableSchema = { tableName: tables[0], columnInfo: [] };
                }
            }
            
            if (!APP_STATE.databases) {
                APP_STATE.databases = {};
            }
            if (!APP_STATE.databases.mysql) {
                APP_STATE.databases.mysql = {};
            }
            APP_STATE.databases.mysql.connectionId = connectionId;
            APP_STATE.databases.mysql.connectionInfo = connectionData.MySQL;
            APP_STATE.databases.mysql.dbType = 'MySQL';
            APP_STATE.databases.mysql.tables = tables;
            APP_STATE.databases.mysql.tableSchema = tableSchema;
            APP_STATE.databases.mysql.currentTable = tables[0] || null;
            APP_STATE.databases.current = APP_STATE.databases.mysql;
            
            APP_STATE.databases.tablePagination = {
                currentPage: 1,
                totalPages: 1,
                totalRows: 0,
                pageSize: 50,
                currentTableName: null
            };

            // 【修改】MySQL连接成功后，自动创建新的数据库会话（仅本地，不调用API）
            try {
                console.log('MySQL连接成功，创建本地数据库会话...');

                // 生成本地会话ID（前端生成，不调用后端API）
                const dbSessionId = 'db_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
                const dbSession = {
                    id: dbSessionId,
                    model: CONFIG.DEFAULTS?.MODEL || 'deepseek',
                    createdAt: Date.now(),
                    messages: [],
                    isDatabase: true,
                    fileId: null,
                    type: 'database',
                    dbType: 'mysql',  // 标记数据库类型
                    isLocal: true     // 标记为本地会话
                };

                // 使用SessionManager设置MySQL数据库会话
                if (typeof PageSessionManager !== 'undefined' && PageSessionManager.setDatabaseSession) {
                    PageSessionManager.setDatabaseSession('mysql', dbSession);
                    console.log('✓ MySQL数据库本地会话创建成功:', dbSessionId);
                } else {
                    // 降级方案
                    APP_STATE.currentSession = dbSession;
                    console.log('⚠ 使用降级方案设置MySQL数据库会话');
                }
            } catch (sessionError) {
                console.warn('⚠ 创建MySQL数据库本地会话异常:', sessionError);
                console.warn('用户需要在数据库对话页面手动点击"新建会话"');
            }

            document.dispatchEvent(new CustomEvent('databaseConnected', {
                detail: {
                    connectionId: APP_STATE.databases.mysql.connectionId,
                    connectionInfo: APP_STATE.databases.mysql.connectionInfo,
                    tables: APP_STATE.databases.mysql.tables,
                    tableSchema: APP_STATE.databases.mysql.tableSchema
                }
            }));

            const databasePage = document.getElementById('database-page');
            if (databasePage && databasePage.classList.contains('active')) {
                updateConnectedDatabaseDisplay();
            }

            switchToDatabaseChatPage({
                connectionId,
                tables,
                tableSchema,
                tableData
            });
        } else {
            const errorMsg = response?.message || response?.errorMsg || '连接失败，请检查连接参数';
            if (typeof showMessage === 'function') {
                showMessage(errorMsg, 'error');
            } else {
                alert(errorMsg);
            }
            console.error('MySQL连接失败:', response);
        }
    } catch (error) {
        console.error('MySQL连接异常:', error);
        const errorMsg = error.message || '连接过程中发生错误';
        if (typeof showMessage === 'function') {
            showMessage(errorMsg, 'error');
        } else {
            alert(errorMsg);
        }
    } finally {
        // 隐藏加载状态
        if (typeof hideLoading === 'function') {
            hideLoading();
        }
    }
}

// 已删除测试连接功能

// 处理SQLite连接
async function handleSQLiteConnect(event) {
    event.preventDefault();
    
    // 获取fileId
    const fileIdInput = document.getElementById('sqlite-file-id');
    const fileId = fileIdInput ? fileIdInput.value.trim() : '';
    
    if (!fileId) {
        if (typeof showMessage === 'function') {
            showMessage('请先上传SQLite文件', 'error');
        } else {
            alert('请先上传SQLite文件');
        }
        return;
    }
    
    // 获取文件信息
    const selectedFile = window._selectedSQLiteFile;
    const fileName = selectedFile ? selectedFile.name : '未知文件';
    const filePath = selectedFile ? `本地文件: ${selectedFile.name}` : '未知路径';

    const connectionInfo = { type: 'SQLite', fileId, fileName, filePath };
    const connectionData = { type: 'SQLite', SQLite: { fileId, readonly: false } };
    
    console.log('连接SQLite数据库，参数:', connectionInfo);
    
    try {
        if (typeof showLoading === 'function') {
            showLoading();
        }
        
        const response = await connectDatabase(connectionData);
        
        if (response && response.success && response.result) {
            const connectionId = response.result.connectionId;
            if (typeof showMessage === 'function') {
                showMessage('SQLite数据库连接成功！', 'success');
            } else {
                alert('SQLite数据库连接成功！');
            }
            console.log('SQLite连接成功，connectionId:', connectionId);
            
            const tablesRes = await getTableList(connectionId);
            const tables = (tablesRes && tablesRes.success && tablesRes.result && tablesRes.result.tables) ? tablesRes.result.tables : [];
            let tableSchema = null;
            let tableData = [];
            if (tables.length > 0) {
                const firstTableRes = await getTableData(connectionId, tables[0]);
                if (firstTableRes && firstTableRes.success && firstTableRes.result && firstTableRes.result.tableSchema) {
                    tableSchema = { ...firstTableRes.result.tableSchema, tableName: tables[0] };
                    tableData = firstTableRes.result.tableSchema.tableData || [];
                } else {
                    tableSchema = { tableName: tables[0], columnInfo: [] };
                }
            }
            
            if (!APP_STATE.databases) {
                APP_STATE.databases = {};
            }
            if (!APP_STATE.databases.sqlite) {
                APP_STATE.databases.sqlite = {};
            }
            APP_STATE.databases.sqlite.connectionId = connectionId;
            APP_STATE.databases.sqlite.connectionInfo = connectionInfo;
            APP_STATE.databases.sqlite.dbType = 'SQLite';
            APP_STATE.databases.sqlite.tables = tables;
            APP_STATE.databases.sqlite.tableSchema = tableSchema;
            APP_STATE.databases.sqlite.currentTable = tables[0] || null;
            APP_STATE.databases.current = APP_STATE.databases.sqlite;

            try {
                console.log('SQLite连接成功，创建本地数据库会话...');
                const dbSessionId = 'db_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
                const dbSession = {
                    id: dbSessionId,
                    model: CONFIG.DEFAULTS?.MODEL || 'deepseek',
                    createdAt: Date.now(),
                    messages: [],
                    isDatabase: true,
                    fileId: null,
                    type: 'database',
                    dbType: 'sqlite',
                    isLocal: true
                };
                if (typeof PageSessionManager !== 'undefined' && PageSessionManager.setDatabaseSession) {
                    PageSessionManager.setDatabaseSession('sqlite', dbSession);
                } else {
                    APP_STATE.currentSession = dbSession;
                }
            } catch (sessionError) {
                console.warn('创建SQLite数据库本地会话异常:', sessionError);
            }

            document.dispatchEvent(new CustomEvent('databaseConnected', {
                detail: {
                    connectionId: APP_STATE.databases.sqlite.connectionId,
                    connectionInfo: APP_STATE.databases.sqlite.connectionInfo,
                    tables: APP_STATE.databases.sqlite.tables,
                    tableSchema: APP_STATE.databases.sqlite.tableSchema
                }
            }));

            const databasePage = document.getElementById('database-page');
            if (databasePage && databasePage.classList.contains('active')) {
                updateConnectedDatabaseDisplay();
            }

            switchToDatabaseChatPage({
                connectionId,
                tables,
                tableSchema,
                tableData
            });
        } else {
            const errorMsg = response?.message || '连接失败，请检查文件路径';
            if (typeof showMessage === 'function') {
                showMessage(errorMsg, 'error');
            } else {
                alert(errorMsg);
            }
            console.error('SQLite连接失败:', response);
        }
    } catch (error) {
        console.error('SQLite连接异常:', error);
        const errorMsg = error.message || '连接过程中发生错误';
        if (typeof showMessage === 'function') {
            showMessage(errorMsg, 'error');
        } else {
            alert(errorMsg);
        }
    } finally {
        // 隐藏加载状态
        if (typeof hideLoading === 'function') {
            hideLoading();
        }
    }
}

// 已删除测试连接功能

// 处理SQLite文件浏览
function handleSQLiteBrowse(event) {
    event.preventDefault();
    
    // 触发文件选择对话框
    const fileInput = document.getElementById('sqlite-file-input');
    if (fileInput) {
        fileInput.click();
    }
}

// 处理SQLite文件选择
function handleSQLiteFileSelect(event) {
    const file = event.target.files[0];
    if (!file) {
        return;
    }
    
    // 验证文件类型
    const validExtensions = ['.db', '.sqlite', '.sqlite3'];
    const fileName = file.name.toLowerCase();
    const isValid = validExtensions.some(ext => fileName.endsWith(ext));
    
    if (!isValid) {
        if (typeof showMessage === 'function') {
            showMessage('请选择有效的SQLite文件（.db, .sqlite, .sqlite3）', 'error');
        } else {
            alert('请选择有效的SQLite文件（.db, .sqlite, .sqlite3）');
        }
        event.target.value = '';
        return;
    }
    
    // 显示文件路径
    const filePathInput = document.getElementById('sqlite-file-path');
    if (filePathInput) {
        filePathInput.value = file.name;
    }
    
    // 显示上传按钮
    const uploadBtn = document.getElementById('sqlite-upload-btn');
    if (uploadBtn) {
        uploadBtn.style.display = 'inline-block';
    }
    
    // 保存文件对象到全局变量，用于上传
    window._selectedSQLiteFile = file;
}

// 处理SQLite文件上传
async function handleSQLiteUpload(event) {
    event.preventDefault();
    
    const file = window._selectedSQLiteFile;
    if (!file) {
        if (typeof showMessage === 'function') {
            showMessage('请先选择SQLite文件', 'error');
        } else {
            alert('请先选择SQLite文件');
        }
        return;
    }
    
    try {
        if (typeof showLoading === 'function') {
            showLoading('上传SQLite文件中...');
        }
        
        // 上传SQLite文件
        const response = await FileAPI.uploadSQLiteFile(file);
        
        if (response && response.success) {
            // 保存fileId
            const fileIdInput = document.getElementById('sqlite-file-id');
            if (fileIdInput) {
                fileIdInput.value = response.result?.fileId || '';
            }
            
            if (typeof showMessage === 'function') {
                showMessage('SQLite文件上传成功', 'success');
            } else {
                alert('SQLite文件上传成功');
            }
        } else {
            const errorMsg = response?.message || response?.errorMsg || '上传失败';
            if (typeof showMessage === 'function') {
                showMessage(errorMsg, 'error');
            } else {
                alert(errorMsg);
            }
        }
    } catch (error) {
        console.error('SQLite文件上传异常:', error);
        const errorMsg = error.message || '上传过程中发生错误';
        if (typeof showMessage === 'function') {
            showMessage(errorMsg, 'error');
        } else {
            alert(errorMsg);
        }
    } finally {
        if (typeof hideLoading === 'function') {
            hideLoading();
        }
    }
}

// 更新已连接数据库的显示
function updateConnectedDatabaseDisplay() {
    const connectedSection = document.getElementById('connected-db-section');
    const connectedList = document.getElementById('connected-db-list');
    
    if (!connectedSection || !connectedList) return;
    
    // 检查是否有已保存的连接信息
    if (APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.connectionId && APP_STATE.databases.current.connectionInfo) {
        // 显示已连接区域
        connectedSection.style.display = 'block';
        
        // 生成已连接数据库卡片
        const connectionInfo = APP_STATE.databases.current.connectionInfo;
        const displayName = getDatabaseDisplayName(connectionInfo);
        const metaInfo = getDatabaseMetaInfo(connectionInfo);
        
        connectedList.innerHTML = `
            <div class="connected-db-card">
                <div class="db-card-header">
                    <i class="fas fa-database"></i>
                    <div class="db-card-info">
                        <h4>${displayName}</h4>
                        <p class="db-card-meta">${metaInfo}</p>
                    </div>
                    <span class="db-status-badge connected">已连接</span>
                </div>
                <div class="db-card-actions">
                    <button class="btn-use-db" onclick="useConnectedDatabase()">
                        <i class="fas fa-arrow-right"></i> 使用此连接
                    </button>
                    <button class="btn-disconnect-db" onclick="disconnectCurrentDatabase()">
                        <i class="fas fa-unlink"></i> 断开连接
                    </button>
                </div>
            </div>
        `;
    } else {
        // 隐藏已连接区域
        connectedSection.style.display = 'none';
    }
}

// 辅助函数：获取数据库显示名称
function getDatabaseDisplayName(connectionInfo) {
    // 如果有type字段，使用type判断
    if (connectionInfo.type === 'MySQL' || connectionInfo.type === 'mysql') {
        return `${connectionInfo.host}:${connectionInfo.port || 3306} / ${connectionInfo.name || connectionInfo.database || '未知数据库'}`;
    } else if (connectionInfo.type === 'sqlite' || connectionInfo.type === 'SQLite') {
        return connectionInfo.fileName || '未知文件';
    }
    // 如果没有type字段，通过字段特征判断（从会话中恢复的MySQL连接）
    if (connectionInfo.host && connectionInfo.port && connectionInfo.name) {
        return `${connectionInfo.host}:${connectionInfo.port} / ${connectionInfo.name}`;
    }
    return '未知数据库';
}

// 辅助函数：获取数据库元信息
function getDatabaseMetaInfo(connectionInfo) {
    // 如果有type字段，使用type判断
    if (connectionInfo.type === 'MySQL' || connectionInfo.type === 'mysql') {
        return `用户: ${connectionInfo.username || '未知'} | 字符集: ${connectionInfo.charset || 'utf8mb4'}`;
    } else if (connectionInfo.type === 'sqlite' || connectionInfo.type === 'SQLite') {
        return `路径: ${connectionInfo.filePath || '未知路径'}`;
    }
    // 如果没有type字段，通过字段特征判断（从会话中恢复的MySQL连接）
    if (connectionInfo.username && connectionInfo.charset) {
        return `用户: ${connectionInfo.username} | 字符集: ${connectionInfo.charset}`;
    }
    return '';
}

// 使用已连接的数据库
function useConnectedDatabase() {
    if (!APP_STATE.databases || !APP_STATE.databases.current || !APP_STATE.databases.current.connectionId) {
        if (typeof showMessage === 'function') {
            showMessage('数据库连接信息不存在', 'error');
        } else {
            alert('数据库连接信息不存在');
        }
        return;
    }
    
    // 跳转到数据库对话页面
    // 使用已保存的信息跳转
    if (APP_STATE.databases.current.tables && APP_STATE.databases.current.tables.length > 0) {
        // 使用已保存的信息跳转
        switchToDatabaseChatPage({
            tables: APP_STATE.databases.current.tables,
            tableSchema: APP_STATE.databases.current.tableSchema,
            connectionId: APP_STATE.databases.current.connectionId
        });
    } else {
        // 提示用户重新连接或加载表信息
        if (typeof showMessage === 'function') {
            showMessage('数据库表信息不存在，请重新连接数据库', 'warning');
        } else {
            alert('数据库表信息不存在，请重新连接数据库');
        }
    }
}

// 断开当前数据库连接
async function disconnectCurrentDatabase() {
    if (!APP_STATE.databases || !APP_STATE.databases.current || !APP_STATE.databases.current.connectionId) {
        if (typeof showMessage === 'function') {
            showMessage('没有已连接的数据库', 'warning');
        } else {
            alert('没有已连接的数据库');
        }
        return;
    }
    
    if (!confirm('确定要断开当前数据库连接吗？')) {
        return;
    }
    
    try {
        const connectionId = APP_STATE.databases.current.connectionId;
        const response = await disconnectDatabase(connectionId);
        
        if (response && response.success) {
            // 【修改】根据当前数据库类型清空对应的会话
            const currentDbType = APP_STATE.databases.current?.connectionInfo?.type?.toLowerCase();

            if (currentDbType === 'mysql') {
                // 清空MySQL会话
                if (typeof SessionManager !== 'undefined' && SessionManager.clearDatabaseSession) {
                    SessionManager.clearDatabaseSession('mysql');
                    console.log('✓ 已清空MySQL数据库会话');
                }
            } else if (currentDbType === 'sqlite') {
                // 清空SQLite会话
                if (typeof SessionManager !== 'undefined' && SessionManager.clearDatabaseSession) {
                    SessionManager.clearDatabaseSession('sqlite');
                    console.log('✓ 已清空SQLite数据库会话');
                }
            }

            // 清空当前连接信息
            if (APP_STATE.databases.current) {
                APP_STATE.databases.current.connectionId = null;
                APP_STATE.databases.current.connectionInfo = null;
                APP_STATE.databases.current.tables = [];
                APP_STATE.databases.current.tableSchema = null;
                APP_STATE.databases.current.currentTable = null;
            }

            // 清空聊天消息区域和结果分析区域
            const dbChatMessages = document.getElementById('db-chat-messages');
            if (dbChatMessages) {
                dbChatMessages.innerHTML = '';
            }
            const dbResultContent = document.getElementById('db-result-content');
            if (dbResultContent) {
                dbResultContent.innerHTML = '';
            }

            // 更新显示
            updateConnectedDatabaseDisplay();
            
            if (typeof showMessage === 'function') {
                showMessage('数据库连接已断开', 'success');
            } else {
                alert('数据库连接已断开');
            }
        } else {
            const errorMsg = response?.message || '断开连接失败';
            if (typeof showMessage === 'function') {
                showMessage(errorMsg, 'error');
            } else {
                alert(errorMsg);
            }
        }
    } catch (error) {
        console.error('断开连接异常:', error);
        const errorMsg = error.message || '断开连接时发生错误';
        if (typeof showMessage === 'function') {
            showMessage(errorMsg, 'error');
        } else {
            alert(errorMsg);
        }
    }
}

// 将函数暴露为全局函数
window.useConnectedDatabase = useConnectedDatabase;
window.disconnectCurrentDatabase = disconnectCurrentDatabase;

// 连接数据库API（按类型扩展：database.type + database.MySQL / database.SQLite）
// connectionData: { type: 'MySQL', MySQL: { host, port, name, username, password, charset } } 或 { type: 'SQLite', SQLite: { fileId, readonly? } }
async function connectDatabase(connectionData) {
    console.log('[connectDatabase] 连接数据库请求:', { type: connectionData?.type, [connectionData?.type]: connectionData?.[connectionData.type] ? { ...connectionData[connectionData.type], password: connectionData[connectionData.type].password ? '***' : undefined } : undefined });
    
    const url = CONFIG.API_BASE_URL + '/api/db/connect';
    const data = {
        requestId: APP_STATE.user.requestId,
        userId: APP_STATE.user.id,
        sessionId: SessionManager.getSessionId() || '',
        database: connectionData
    };
    const logDb = { ...data.database };
    if (logDb.MySQL && logDb.MySQL.password) logDb.MySQL = { ...logDb.MySQL, password: '***' };
    console.log('[connectDatabase] 发送的请求数据:', { ...data, database: logDb });
    return await apiRequest(url, {
        method: 'POST',
        body: JSON.stringify(data)
    });
}

// 获取表列表API
async function getTableList(dbConnectId) {
    if (!dbConnectId) {
        console.warn('getTableList: dbConnectId为空');
        return { success: false, message: '连接ID不能为空' };
    }
    const requestId = APP_STATE.user.requestId || generateRequestId();
    const sessionId = SessionManager.getSessionId() || '';
    const url = `${CONFIG.API_BASE_URL}/api/db/tables?requestId=${requestId}&sessionId=${sessionId}&dbConnectId=${encodeURIComponent(dbConnectId)}`;
    try {
        const response = await apiRequest(url, { method: 'GET' });
        return response;
    } catch (error) {
        console.error('获取表列表异常:', error);
        return { success: false, message: error.message || '获取表列表失败' };
    }
}

// 断开数据库连接API
async function disconnectDatabase(connectionId) {
    if (!connectionId) {
        console.warn('disconnectDatabase: connectionId为空');
        return { success: false, message: '连接ID不能为空' };
    }
    
    console.log('[disconnectDatabase] 断开数据库连接请求:', connectionId);
    
    const url = CONFIG.API_BASE_URL + '/api/db/disconnect';
    
    const data = {
        requestId: APP_STATE.user.requestId,
        sessionId: SessionManager.getSessionId() || '',
        connectionId: connectionId
    };
    
    try {
        const response = await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
        
        if (response && response.success) {
            console.log('断开数据库连接成功:', connectionId);
            // 清除本地保存的连接信息
            if (APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.connectionId === connectionId) {
                // 清理当前数据库
                if (APP_STATE.databases.current.connectionId === connectionId) {
                    APP_STATE.databases.current.connectionId = null;
                    APP_STATE.databases.current.connectionInfo = null;
                    APP_STATE.databases.current.tables = [];
                    APP_STATE.databases.current.tableSchema = null;
                    APP_STATE.databases.current.currentTable = null;
                }
            }
        }
        
        return response;
    } catch (error) {
        console.error('断开数据库连接异常:', error);
        return { success: false, message: error.message || '断开连接失败' };
    }
}

// 获取表数据API
async function getTableData(dbConnectId, tableName, pageNumber = 1, pageSize = 50) {
    if (!dbConnectId || !tableName) {
        console.warn('getTableData: dbConnectId或tableName为空');
        return { success: false, message: '连接ID和表名不能为空' };
    }
    
    console.log('[getTableData] 获取表数据请求:', { dbConnectId, tableName, pageNumber, pageSize });
    
    const requestId = APP_STATE.user.requestId || generateRequestId();
    const sessionId = SessionManager.getSessionId() || '';
    const url = `${CONFIG.API_BASE_URL}/api/db/table/data`;
    const body = {
        requestId,
        sessionId,
        dbConnectId,
        tableName,
        forceOriginal: false,
        pageNumber: pageNumber,
        pageSize: pageSize
    };
    
    console.log('[getTableData] 完整请求体:', JSON.stringify(body, null, 2));
    
    try {
        const response = await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(body)
        });
        
        console.log('[getTableData] 响应:', response);
        
        return response;
    } catch (error) {
        console.error('获取表数据异常:', error);
        return { success: false, message: error.message || '获取表数据失败' };
    }
}

// 获取表数据API（支持forceOriginal参数）
async function getTableDataWithForceOriginal(dbConnectId, tableName, pageNumber = 1, pageSize = 50, forceOriginal = false) {
    if (!dbConnectId || !tableName) {
        console.warn('getTableDataWithForceOriginal: dbConnectId或tableName为空');
        return { success: false, message: '连接ID和表名不能为空' };
    }
    
    console.log('[getTableDataWithForceOriginal] 获取表数据请求:', { dbConnectId, tableName, pageNumber, pageSize, forceOriginal });
    
    const requestId = APP_STATE.user.requestId || generateRequestId();
    const sessionId = SessionManager.getSessionId() || '';
    const url = `${CONFIG.API_BASE_URL}/api/db/table/data`;
    const body = {
        requestId,
        sessionId,
        dbConnectId,
        tableName,
        forceOriginal: forceOriginal,
        pageNumber: pageNumber,
        pageSize: pageSize
    };
    
    try {
        const response = await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(body)
        });
        
        console.log('[getTableDataWithForceOriginal] 响应:', response);
        
        return response;
    } catch (error) {
        console.error('获取表数据异常:', error);
        return { success: false, message: error.message || '获取表数据失败' };
    }
}

// 生成请求ID的辅助函数
function generateRequestId() {
    return 'req_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
}

// 已删除测试连接功能

// 初始化数据库对话页面聊天功能
function initDatabaseChat() {
    console.log('initDatabaseChat: 开始初始化数据库对话页面聊天功能');
    
    // 绑定发送按钮
    const dbSendBtn = document.getElementById('db-send-btn');
    if (dbSendBtn) {
        dbSendBtn.addEventListener('click', handleDatabaseSendMessage);
        console.log('initDatabaseChat: 发送按钮事件已绑定');
    } else {
        console.error('initDatabaseChat: 找不到 db-send-btn 元素');
    }
    
    // 绑定消息输入框
    const dbMessageInput = document.getElementById('db-message-input');
    if (dbMessageInput) {
        dbMessageInput.addEventListener('keydown', function(e) {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                handleDatabaseSendMessage();
            }
        });
        console.log('initDatabaseChat: 消息输入框事件已绑定');
    } else {
        console.error('initDatabaseChat: 找不到 db-message-input 元素');
    }
    
    // 绑定新建会话按钮
    const dbNewSessionBtn = document.getElementById('db-new-session-btn');
    if (dbNewSessionBtn) {
        dbNewSessionBtn.addEventListener('click', handleDatabaseNewSession);
        console.log('initDatabaseChat: 新建会话按钮事件已绑定');
    } else {
        console.error('initDatabaseChat: 找不到 db-new-session-btn 元素');
    }
    
    // 初始化模型选择下拉框
    updateDatabaseModelSelect();
}

// 更新数据库对话页面的模型选择下拉框
function updateDatabaseModelSelect() {
    const dbModelSelect = document.getElementById('db-model-select');
    if (!dbModelSelect) return;
    
    dbModelSelect.innerHTML = '';
    
    if (APP_STATE.models && APP_STATE.models.length > 0) {
        APP_STATE.models.forEach(model => {
            const option = document.createElement('option');
            option.value = model.modelName || model.name;
            option.textContent = model.modelName || model.name;
            dbModelSelect.appendChild(option);
        });
    }
}

// 处理数据库对话页面新建会话
async function handleDatabaseNewSession() {
    console.log('handleDatabaseNewSession: 创建新的数据库会话');

    try {
        const dbModelSelect = document.getElementById('db-model-select');
        const selectedModel = dbModelSelect ? dbModelSelect.value : (APP_STATE.models[0]?.modelName || 'deepseek');

        if (!selectedModel) {
            showMessage('请先选择模型', 'warning');
            return;
        }

        // 检查是否已连接数据库
        if (!APP_STATE.databases || !APP_STATE.databases.current || !APP_STATE.databases.current.connectionInfo) {
            showMessage('请先连接数据库', 'warning');
            return;
        }

        showLoading('创建会话中...');

        // 【修复】始终通过API创建后端会话，确保chatSessionId在后端注册
        // 这样后端才能通过verifyOwnership验证会话所有权
        // Excel助手页面也是这样处理的（见chat.js的createNewSession函数）
        console.log('通过API创建后端数据库会话');
        
        // 获取当前数据库连接信息
        const dbConnectionInfo = APP_STATE.databases.current.connectionInfo;
        console.log('使用数据库连接信息:', dbConnectionInfo);
        
        // 创建会话时传递sessionType和dbConnectionInfo
        const response = await ModelAPI.createSession(selectedModel, 'database', dbConnectionInfo);
        
        if (response && response.success && response.result) {
            const chatSessionId = response.result.chatSessionId;
            if (!chatSessionId) {
                showMessage('创建会话失败：响应格式错误', 'error');
                return;
            }
            const newDatabaseSession = {
                id: chatSessionId,
                model: response.result.modelName || selectedModel,
                createdAt: Date.now(),
                messages: [],
                isDatabase: true,  // 标记为数据库会话
                fileId: null,      // 数据库会话不使用fileId
                type: 'database',  // 标记会话类型
                sessionType: 'database',  // 会话类型
                dbConnectId: APP_STATE.databases.current.connectionId,  // 数据库连接ID
                dbConnectionInfo: dbConnectionInfo,  // 数据库连接信息
                isLocal: false  // 始终为false，因为通过API创建的后端会话
            };

            // 【修改】使用SessionManager设置数据库会话
            // 由于数据库对话页面可以处理MySQL和SQLite，这里统一设置为database类型
            if (typeof PageSessionManager !== 'undefined' && PageSessionManager.setDatabaseSession) {
                PageSessionManager.setDatabaseSession('current', newDatabaseSession);
                console.log('✓ 已通过SessionManager设置数据库会话');
            } else {
                // 降级到直接设置（向后兼容）
                APP_STATE.currentSession = newDatabaseSession;
                console.log('⚠ SessionManager不可用，使用降级方案设置数据库会话');
            }
            
            console.log('========== 数据库会话已创建 ==========');
            console.log('当前会话状态:', APP_STATE.currentSession);
            console.log('会话ID:', APP_STATE.currentSession.id);
            console.log('===================================');
            
            // 【修复】如果存在 SQLite 文件，自动关联文件和会话
            const sqliteFileId = APP_STATE.databases.current?.connectionInfo?.fileId;
            if (sqliteFileId) {
                try {
                    console.log('自动关联SQLite文件到会话:', sqliteFileId, chatSessionId);
                    const bindResponse = await FileAPI.handleFileChatSessionMap(sqliteFileId, chatSessionId);
                    if (bindResponse && bindResponse.success) {
                        console.log('✓ SQLite文件关联成功');
                        // 更新会话的fileId
                        newDatabaseSession.fileId = sqliteFileId;
                        APP_STATE.currentSession.fileId = sqliteFileId;
                    } else {
                        console.warn('⚠ SQLite文件关联失败，但不影响会话创建');
                    }
                } catch (error) {
                    console.warn('⚠ SQLite文件关联异常，但不影响会话创建:', error);
                }
            }
            
            // 清空消息显示区域
            const dbChatMessages = document.getElementById('db-chat-messages');
            if (dbChatMessages) {
                dbChatMessages.innerHTML = '';
            }
            
            showMessage('会话创建成功', 'success');
            console.log('数据库会话创建成功:', chatSessionId);
        } else {
            const errorMsg = response?.message || '创建会话失败';
            showMessage(errorMsg, 'error');
            console.error('数据库会话创建失败:', response);
        }
    } catch (error) {
        console.error('创建数据库会话异常:', error);
        showMessage('创建会话失败: ' + error.message, 'error');
    } finally {
        hideLoading();
    }
}

// 处理数据库对话页面发送消息
async function handleDatabaseSendMessage() {
    const dbMessageInput = document.getElementById('db-message-input');
    const dbSendBtn = document.getElementById('db-send-btn');
    
    if (!dbMessageInput || !dbSendBtn) {
        console.error('找不到数据库对话页面的输入元素');
        return;
    }
    
    const message = dbMessageInput.value.trim();
    if (!message) {
        return;
    }
    
    // 【修复】检查是否有会话，需要更严格的检查：确保 currentSession 存在、有 id、且 id 不为空字符串
    if (!APP_STATE.currentSession || 
        !APP_STATE.currentSession.id || 
        APP_STATE.currentSession.id === '' ||
        APP_STATE.currentSession.id === null ||
        APP_STATE.currentSession.id === undefined) {
        console.warn('发送数据库消息失败: 会话检查失败', {
            hasCurrentSession: !!APP_STATE.currentSession,
            sessionId: APP_STATE.currentSession?.id,
            sessionType: APP_STATE.currentSession?.isDatabase ? 'database' : 'chat'
        });
        showMessage('请先创建会话后再发送消息。请点击"新建会话"按钮创建聊天会话', 'warning');
        return;
    }
    
    console.log('发送数据库消息: 会话检查通过', {
        sessionId: APP_STATE.currentSession.id,
        sessionType: APP_STATE.currentSession.isDatabase ? 'database' : 'chat'
    });
    
    // 检查是否有选中的表
    if (!APP_STATE.databases || !APP_STATE.databases.current || !APP_STATE.databases.current.currentTable) {
        showMessage('请先选择要查询的表', 'warning');
        return;
    }
    
    // 清空输入框
    dbMessageInput.value = '';
    
    // 禁用发送按钮
    dbSendBtn.disabled = true;
    
    // 显示用户消息
    const dbChatMessages = document.getElementById('db-chat-messages');
    if (dbChatMessages) {
        const userMessageEl = document.createElement('div');
        userMessageEl.className = 'message user';
        userMessageEl.innerHTML = `
            <div class="message-content">${message}</div>
            <div class="message-time">${formatTime(Date.now())}</div>
        `;
        dbChatMessages.appendChild(userMessageEl);
        scrollToBottom(dbChatMessages);
    }
    
    try {
        const chatSessionId = APP_STATE.currentSession.id;
        const messageId = 'msg_' + Date.now();
        
        // 获取数据库连接ID
        const dbConnectId = APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.connectionId
            ? APP_STATE.databases.current.connectionId
            : null;
        
        if (!dbConnectId) {
            showMessage('请先连接数据库', 'warning');
            dbSendBtn.disabled = false;
            return;
        }
        
        // 获取数据库名和表名
        // 根据数据库类型获取数据库名称
        let databaseName = null;
        if (APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.connectionInfo) {
            const connInfo = APP_STATE.databases.current.connectionInfo;
            if (connInfo.type === 'SQLite') {
                // SQLite使用fileId作为数据库名称
                databaseName = connInfo.fileId || null;
            } else if (connInfo.type === 'MySQL') {
                // MySQL使用name字段
                databaseName = connInfo.name || null;
            }
        }
        
        const tableName = APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.currentTable
            ? APP_STATE.databases.current.currentTable
            : null;

        console.log('发送数据库消息，使用连接ID:', dbConnectId, '数据库类型:', APP_STATE.databases?.current?.connectionInfo?.type, '数据库名:', databaseName, '表名:', tableName);

        // 用于累积流式内容
        let streamContent = '';

        // 【修复】后端要求chatSessionId不能为空，所以即使isLocal为true，也要使用有效的chatSessionId
        // 本地会话也有有效的chatSessionId（在创建时生成），不应该传null
        const actualChatSessionId = chatSessionId;
        console.log('========== 准备发送数据库消息 ==========');
        console.log('chatSessionId:', actualChatSessionId);
        console.log('isLocal:', APP_STATE.currentSession?.isLocal);
        console.log('httpSessionId:', SessionManager.getSessionId());
        console.log('dbConnectId:', dbConnectId);
        console.log('databaseName:', databaseName);
        console.log('tableName:', tableName);
        console.log('message:', message);
        console.log('==========================================');
        
        // 【修复】验证chatSessionId不能为空
        if (!actualChatSessionId || actualChatSessionId.trim() === '') {
            console.error('发送数据库消息失败: chatSessionId为空');
            showMessage('会话ID无效，请重新创建会话', 'error');
            dbSendBtn.disabled = false;
            return;
        }
        
        // 【修复】验证httpSessionId不能为空
        const httpSessionId = SessionManager.getSessionId();
        if (!httpSessionId || httpSessionId.trim() === '') {
            console.error('发送数据库消息失败: httpSessionId为空');
            showMessage('HTTP会话ID无效，请重新登录', 'error');
            dbSendBtn.disabled = false;
            return;
        }

        // 发送消息（使用数据库连接ID、数据库名和表名）
        await ModelAPI.sendMessage(
            actualChatSessionId,
            message,
            'database',  // 【修复】数据库场景chatType为database
            null,  // 数据库会话不使用fileId
            dbConnectId,  // 传递数据库连接ID
            databaseName,  // 传递数据库名
            tableName,  // 传递表名
            // onChunk回调
            (chunk) => {
                streamContent += chunk;
                console.log('[数据库onChunk] 收到chunk，长度:', chunk.length, '累计长度:', streamContent.length);
                
                // 使用displayStreamingMessage来解析和显示消息（支持按标签解析和直接输出）
                displayStreamingMessage(messageId, streamContent);
            },
            // onComplete回调
            () => {
                console.log('========== 数据库消息流式传输完成 ==========');
                console.log('streamContent总长度:', streamContent.length);
                console.log('streamContent包含CHART_DATA:', streamContent.includes('<CHART_DATA>'));
                
                // 【修复】检查是否真的收到了数据，如果没有收到数据，不应该显示成功
                if (!streamContent || streamContent.trim() === '') {
                    console.error('========== 数据库消息流式传输完成但未收到任何数据 ==========');
                    console.error('这可能表示后端未收到请求或请求处理失败');
                    showMessage('消息发送失败：未收到后端响应数据', 'error');
                    return;
                }
                
                // 添加到会话消息列表
                if (APP_STATE.currentSession) {
                    APP_STATE.currentSession.messages.push({
                        id: messageId,
                        role: 'assistant',
                        content: streamContent,
                        timestamp: Date.now()
                    });
                }

                // 触发消息完成事件，供PreviewManager监听
                document.dispatchEvent(new CustomEvent('messageCompleted', {
                    detail: {
                        content: streamContent,
                        messageId: messageId
                    }
                }));
                
                // 检查是否包含图表数据，如果包含则切换到结果分析tab并渲染
                console.log('========== 开始检查数据库CHART_DATA ==========');
                // 使用非贪婪匹配，但确保能匹配到完整的JSON（即使包含换行符）
                // 注意：如果JSON中包含</CHART_DATA>字符串（在JSON字符串值中），应该被转义为<\/CHART_DATA>
                const chartDataMatch = streamContent.match(/<CHART_DATA>([\s\S]*?)<\/CHART_DATA>/);
                console.log('chartDataMatch结果:', chartDataMatch ? '✓ 找到' : '✗ 未找到');
                
                if (chartDataMatch) {
                    console.log('✓ 检测到数据库图表数据标签！');
                    try {
                        let chartDataJson = chartDataMatch[1];
                        console.log('图表数据JSON长度:', chartDataJson.length);
                        console.log('图表数据JSON前200字符:', chartDataJson.substring(0, 200));
                        
                        // 先清理前后空白
                        chartDataJson = chartDataJson.trim();
                        
                        // 使用safeJSONParse解析
                        const chartData = window.safeJSONParse(chartDataJson);
                        if (chartData) {
                            console.log('✓ 解析数据库图表数据成功:', chartData);
                        } else {
                            console.error('✗ 解析数据库图表数据失败（返回null）');
                            console.error('尝试解析的JSON前500字符:', chartDataJson.substring(0, 500));
                            throw new Error('JSON解析返回null');
                        }
                        
                        // 延迟切换标签页，确保DOM已更新
                        setTimeout(() => {
                            console.log('开始切换到数据库结果分析标签页');
                            // 【修复问题1】使用正确的标签页切换函数，只切换数据库对话页面的标签页
                            switchDatabaseChatTab('result-analysis');
                            
                            // 切换后再更新结果分析区域
                            setTimeout(() => {
                                updateDatabaseResultAnalysis(streamContent, chartData);
                                console.log('✓ 数据库结果分析区域更新完成');
                            }, 100);
                        }, 100);
                        
                    } catch (e) {
                        console.error('✗ 在onComplete中解析数据库图表数据失败:', e);
                        console.error('错误堆栈:', e.stack);
                        console.error('尝试解析的JSON:', chartDataMatch[1]);
                    }
                } else {
                    console.log('✗ 未检测到数据库图表数据标签');
                    
                    // 【修复问题2】即使没有CHART_DATA，如果消息包含总结内容，也要追加显示
                    // 检查是否包含TITLE_START或其他分析标签
                    const hasTitle = streamContent.includes('<TITLE_START>');
                    const hasSummary = streamContent.includes('<CHART_DATA>') || streamContent.includes('summary');
                    
                    if (hasTitle || hasSummary) {
                        console.log('检测到分析内容，切换到结果分析标签页并追加显示');
                        setTimeout(() => {
                            // 切换到结果分析标签页
                            switchDatabaseChatTab('result-analysis');
                            
                            // 尝试从内容中提取并显示（即使没有完整的CHART_DATA）
                            setTimeout(() => {
                                updateDatabaseResultAnalysis(streamContent, null);
                                console.log('✓ 数据库结果分析区域更新完成（无CHART_DATA）');
                            }, 100);
                        }, 100);
                    }
                }
                
                showMessage('消息发送成功', 'success');
            },
            // onError回调
            (error) => {
                console.error('发送数据库消息失败:', error);
                // 显示错误消息
                const messageEl = document.getElementById(`message-${messageId}`);
                if (messageEl) {
                    const messageContent = messageEl.querySelector('.message-content');
                    if (messageContent) {
                        messageContent.innerHTML = '<div class="markdown-content">消息发送失败: ' + (error.message || error.toString()) + '</div>';
                    }
                } else {
                    // 如果消息元素不存在，创建一个错误消息
                    if (dbChatMessages) {
                        const errorMessageEl = document.createElement('div');
                        errorMessageEl.id = `message-${messageId}`;
                        errorMessageEl.className = 'message assistant';
                        errorMessageEl.innerHTML = `
                            <div class="message-content">
                                <div class="markdown-content">消息发送失败: ${error.message || error.toString()}</div>
                            </div>
                            <div class="message-time">${formatTime(Date.now())}</div>
                        `;
                        dbChatMessages.appendChild(errorMessageEl);
                        scrollToBottom(dbChatMessages);
                    }
                }
                showMessage('消息发送失败: ' + (error.message || error.toString()), 'error');
            }
        );
        
    } catch (error) {
        console.error('发送数据库消息失败:', error);
        showMessage('消息发送失败: ' + error.message, 'error');
    } finally {
        // 重新启用发送按钮
        dbSendBtn.disabled = false;
    }
}

// 跳转到数据库对话页面
function switchToDatabaseChatPage(dbResult) {
    console.log('跳转到数据库对话页面，数据:', dbResult);
    
    // 清空聊天消息区域和结果分析区域（切换数据库连接时需要）
    const dbChatMessages = document.getElementById('db-chat-messages');
    if (dbChatMessages) {
        dbChatMessages.innerHTML = '';
    }
    const dbResultContent = document.getElementById('db-result-content');
    if (dbResultContent) {
        dbResultContent.innerHTML = '';
    }
    
    // 保存数据库结果到全局状态
    if (!APP_STATE.databases) {
        APP_STATE.databases = {};
    }
    if (!APP_STATE.databases.current) {
        APP_STATE.databases.current = {};
    }
    APP_STATE.databases.current.tables = dbResult.tables || [];
    APP_STATE.databases.current.tableSchema = dbResult.tableSchema || null;
    APP_STATE.databases.current.currentTable = dbResult.tableSchema?.tableName || null;
    APP_STATE.databases.current.connectionId = dbResult.connectionId;
    
    APP_STATE.databases.tablePagination = {
        currentPage: 1,
        totalPages: 1,
        totalRows: 0,
        pageSize: 50,
        currentTableName: null
    };
    
    // 更新表名下拉框
    updateTableSelect(dbResult.tables || [], dbResult.tableSchema?.tableName);
    
    // 【修复】显示表预览 - 后端返回的tableData在result根目录，需要合并到tableSchema中
    if (dbResult.tableSchema) {
        // 构建完整的表结构对象，包含表数据
        const tableSchemaForDisplay = {
            tableName: dbResult.tableSchema.tableName || '',
            columnInfo: dbResult.tableSchema.columnInfo || [],
            tableData: dbResult.tableData || []  // 从result根目录获取tableData
        };
        
        console.log('准备显示表预览:', {
            tableName: tableSchemaForDisplay.tableName,
            columnCount: tableSchemaForDisplay.columnInfo.length,
            rowCount: tableSchemaForDisplay.tableData.length
        });
        
        displayTablePreview(tableSchemaForDisplay);
    } else {
        console.warn('switchToDatabaseChatPage: dbResult.tableSchema为空，无法显示表预览');
    }
    
    // 切换到数据库对话页面（不更新导航栏，因为这是内部页面）
    const pages = document.querySelectorAll('.page');
    pages.forEach(page => page.classList.remove('active'));
    
    const dbChatPage = document.getElementById('db-chat-page');
    if (dbChatPage) {
        dbChatPage.classList.add('active');
    }
    
    // 触发页面切换事件，让PreviewManager更新按钮状态
    document.dispatchEvent(new CustomEvent('pageChanged', {
        detail: { page: 'database' }
    }));
}

// 更新表名下拉框
function updateTableSelect(tables, currentTable) {
    const tableSelect = document.getElementById('db-table-select');
    if (!tableSelect) return;
    
    tableSelect.innerHTML = '';
    
    // 添加默认选项
    const defaultOption = document.createElement('option');
    defaultOption.value = '';
    defaultOption.textContent = '选择表...';
    tableSelect.appendChild(defaultOption);
    
    // 添加表名选项
    tables.forEach(tableName => {
        const option = document.createElement('option');
        option.value = tableName;
        option.textContent = tableName;
        if (tableName === currentTable) {
            option.selected = true;
        }
        tableSelect.appendChild(option);
    });
    
    // 绑定切换事件
    tableSelect.onchange = async function() {
        const selectedTable = this.value;
        if (selectedTable) {
            await switchTable(selectedTable);
        }
    };
    
    // 【修复问题3】在表下拉框后显示数据库类型
    const chatHeader = tableSelect.closest('.chat-header');
    if (chatHeader) {
        // 移除已有的数据库类型显示
        const existingDbTypeLabel = chatHeader.querySelector('.db-type-label');
        if (existingDbTypeLabel) {
            existingDbTypeLabel.remove();
        }
        
        // 如果有数据库类型，添加显示
        if (APP_STATE.databases && APP_STATE.databases.current && APP_STATE.databases.current.dbType) {
            const dbTypeLabel = document.createElement('span');
            dbTypeLabel.className = 'db-type-label';
            dbTypeLabel.style.cssText = 'margin-left: 10px; padding: 5px 12px; background: #667eea; color: white; border-radius: 4px; font-size: 14px; font-weight: 500;';
            dbTypeLabel.textContent = APP_STATE.databases.current.dbType;
            tableSelect.parentNode.insertBefore(dbTypeLabel, tableSelect.nextSibling);
            console.log('已显示数据库类型:', APP_STATE.databases.current.dbType);
        }
    }
}

// 切换表
async function switchTable(tableName) {
    console.log('切换表:', tableName);
    
    if (!tableName) {
        console.warn('切换表: 表名为空');
        return;
    }
    
    try {
        showLoading('加载表数据中...');
        
        // 检查是否有数据库连接ID
        if (!APP_STATE.databases || !APP_STATE.databases.current || !APP_STATE.databases.current.connectionId) {
            console.error('切换表失败: 数据库连接ID不存在');
            showMessage('数据库连接不存在，请重新连接数据库', 'error');
            return;
        }
        
        // 调用后端API获取表数据
        const response = await getTableData(APP_STATE.databases.current.connectionId, tableName);
        
        console.log('[switchTable] 响应数据:', response);
        
        if (response && response.success && response.result) {
            console.log('[switchTable] result内容:', response.result);
            console.log('[switchTable] tableSchema内容:', response.result.tableSchema);
            
            // 【修复】构建表结构对象，包含表名、列信息和表数据
            // 注意：后端返回的tableSchema可能没有tableName，需要从请求参数中获取
            const tableSchema = {
                tableName: tableName,  // 使用请求的表名
                columnInfo: response.result.tableSchema?.columnInfo || [],
                tableData: response.result.tableSchema?.tableData || []
            };
            
            console.log('[switchTable] 构建的表结构:', {
                tableName: tableSchema.tableName,
                columnCount: tableSchema.columnInfo.length,
                rowCount: tableSchema.tableData.length,
                columnInfo: tableSchema.columnInfo,
                firstRow: tableSchema.tableData[0]
            });
            
            // 显示表预览
            displayTablePreview(tableSchema);
            
            // 更新当前表状态
            if (APP_STATE.databases && APP_STATE.databases.current) {
                APP_STATE.databases.current.currentTable = tableName;
                APP_STATE.databases.current.tableSchema = tableSchema;
            }
            
            console.log('切换表成功:', tableName);
        } else {
            const errorMsg = response?.message || '获取表数据失败';
            console.error('切换表失败:', errorMsg, '完整响应:', response);
            showMessage(errorMsg, 'error');
        }
    } catch (error) {
        console.error('切换表异常:', error);
        showMessage('切换表失败: ' + (error.message || '未知错误'), 'error');
    } finally {
        hideLoading();
    }
}

// 显示表预览
function displayTablePreview(tableSchema) {
    console.log('[displayTablePreview] 开始显示表预览:', {
        tableName: tableSchema?.tableName,
        hasColumnInfo: !!(tableSchema?.columnInfo),
        columnCount: tableSchema?.columnInfo?.length || 0,
        hasTableData: !!(tableSchema?.tableData),
        rowCount: tableSchema?.tableData?.length || 0
    });
    
    const uploadPrompt = document.getElementById('table-upload-prompt');
    const tablePreview = document.getElementById('table-preview-content');
    const tableNameEl = document.getElementById('table-name');
    const tableDataEl = document.getElementById('table-data');
    
    if (!uploadPrompt || !tablePreview || !tableNameEl || !tableDataEl) {
        console.error('[displayTablePreview] 表预览元素未找到:', {
            uploadPrompt: !!uploadPrompt,
            tablePreview: !!tablePreview,
            tableNameEl: !!tableNameEl,
            tableDataEl: !!tableDataEl
        });
        return;
    }
    
    // 隐藏提示，显示预览
    uploadPrompt.style.display = 'none';
    tablePreview.style.display = 'flex';
    
    // 更新表名
    tableNameEl.textContent = tableSchema.tableName || '表';
    
    // 更新分页状态
    if (tableSchema.tableData) {
        APP_STATE.databases.tablePagination.currentPage = tableSchema.tableData.currentPage || 1;
        APP_STATE.databases.tablePagination.totalPages = tableSchema.tableData.totalPages || 1;
        APP_STATE.databases.tablePagination.totalRows = tableSchema.tableData.totalRows || 0;
        APP_STATE.databases.tablePagination.pageSize = tableSchema.tableData.pageSize || 50;
        APP_STATE.databases.tablePagination.currentTableName = tableSchema.tableName;
    }
    
    // 清空表格
    tableDataEl.innerHTML = '';
    
    // 创建表头
    const headerRow = document.createElement('tr');
    if (tableSchema.columnInfo && tableSchema.columnInfo.length > 0) {
        tableSchema.columnInfo.forEach(column => {
            const th = document.createElement('th');
            th.textContent = column.name || '';
            headerRow.appendChild(th);
        });
        console.log('[displayTablePreview] 创建表头，列数:', tableSchema.columnInfo.length);
    } else {
        console.warn('[displayTablePreview] 没有列信息，无法创建表头');
    }
    tableDataEl.appendChild(headerRow);
    
    // 创建数据行
    if (tableSchema.tableData && tableSchema.tableData.rows && tableSchema.tableData.rows.length > 0) {
        console.log('[displayTablePreview] 开始创建数据行，行数:', tableSchema.tableData.rows.length);
        tableSchema.tableData.rows.forEach((rowData, rowIndex) => {
            const row = document.createElement('tr');
            const expectedColCount = tableSchema.columnInfo ? tableSchema.columnInfo.length : 0;
            const actualColCount = rowData ? rowData.length : 0;
            
            if (actualColCount !== expectedColCount) {
                console.warn(`[displayTablePreview] 第${rowIndex + 1}行列数不匹配: 期望${expectedColCount}，实际${actualColCount}`);
            }
            
            if (rowData && rowData.length > 0) {
                rowData.forEach((cellData, colIndex) => {
                    const td = document.createElement('td');
                    const columnType = tableSchema.columnInfo && tableSchema.columnInfo[colIndex] ?
                                       tableSchema.columnInfo[colIndex].type : null;
                    if (cellData !== null && cellData !== undefined && isBlobType(columnType)) {
                        td.textContent = decodeBase64ToHex(String(cellData));
                    } else {
                        td.textContent = cellData !== null && cellData !== undefined ? String(cellData) : '';
                    }
                    row.appendChild(td);
                });
            } else {
                for (let i = 0; i < expectedColCount; i++) {
                    const td = document.createElement('td');
                    td.textContent = '';
                    row.appendChild(td);
                }
            }
            tableDataEl.appendChild(row);
        });
        console.log('[displayTablePreview] 数据行创建完成');
    } else {
        console.log('[displayTablePreview] 表为空，显示空表提示');
        const emptyRow = document.createElement('tr');
        const emptyCell = document.createElement('td');
        emptyCell.colSpan = tableSchema.columnInfo ? tableSchema.columnInfo.length : 1;
        emptyCell.textContent = '表为空';
        emptyCell.style.textAlign = 'center';
        emptyCell.style.color = '#6c757d';
        emptyRow.appendChild(emptyCell);
        tableDataEl.appendChild(emptyRow);
    }
    
    console.log('[displayTablePreview] 表预览显示完成');
    
    // 更新当前表状态
    if (APP_STATE.databases && APP_STATE.databases.current) {
        APP_STATE.databases.current.currentTable = tableSchema.tableName;
        APP_STATE.databases.current.tableSchema = tableSchema;
    }
    
    // 显示分页控件
    displayTablePagination();
}

// 显示数据库表分页控件
function displayTablePagination() {
    const paginationContainer = document.getElementById('table-pagination');
    if (!paginationContainer) {
        const tablePreview = document.getElementById('table-preview-content');
        if (tablePreview) {
            const container = document.createElement('div');
            container.id = 'table-pagination';
            container.className = 'pagination-container';
            container.style.cssText = 'display: flex; justify-content: center; align-items: center; margin-top: 20px; gap: 10px;';
            tablePreview.appendChild(container);
        }
    }
    
    const container = document.getElementById('table-pagination');
    if (!container) return;
    
    container.innerHTML = '';
    
    const { currentPage, totalPages, totalRows, pageSize } = APP_STATE.databases.tablePagination;
    
    const infoSpan = document.createElement('span');
    infoSpan.textContent = `共 ${totalRows} 行`;
    infoSpan.style.color = '#6c757d';
    container.appendChild(infoSpan);
    
    const prevBtn = document.createElement('button');
    prevBtn.textContent = '上一页';
    prevBtn.className = 'pagination-btn';
    prevBtn.disabled = currentPage <= 1;
    prevBtn.onclick = () => loadTablePage(currentPage - 1);
    container.appendChild(prevBtn);
    
    const pageSpan = document.createElement('span');
    pageSpan.textContent = `${currentPage} / ${totalPages}`;
    pageSpan.style.margin = '0 10px';
    container.appendChild(pageSpan);
    
    const nextBtn = document.createElement('button');
    nextBtn.textContent = '下一页';
    nextBtn.className = 'pagination-btn';
    nextBtn.disabled = currentPage >= totalPages;
    nextBtn.onclick = () => loadTablePage(currentPage + 1);
    container.appendChild(nextBtn);
    
    const jumpInput = document.createElement('input');
    jumpInput.type = 'number';
    jumpInput.min = '1';
    jumpInput.max = totalPages.toString();
    jumpInput.value = currentPage.toString();
    jumpInput.style.width = '60px';
    jumpInput.style.padding = '5px';
    container.appendChild(jumpInput);
    
    const jumpBtn = document.createElement('button');
    jumpBtn.textContent = '跳转';
    jumpBtn.className = 'pagination-btn';
    jumpBtn.onclick = () => {
        const page = parseInt(jumpInput.value);
        if (page >= 1 && page <= totalPages) {
            loadTablePage(page);
        }
    };
    container.appendChild(jumpBtn);
}

// 加载数据库表指定页数据
async function loadTablePage(pageNumber) {
    if (!APP_STATE.databases.current || !APP_STATE.databases.current.connectionId || !APP_STATE.databases.tablePagination.currentTableName) {
        console.warn('没有当前数据库连接或表');
        return;
    }
    
    try {
        showLoading('加载中...');
        
        const response = await getTableData(
            APP_STATE.databases.current.connectionId,
            APP_STATE.databases.tablePagination.currentTableName,
            pageNumber,
            APP_STATE.databases.tablePagination.pageSize
        );
        
        if (response.success && response.result) {
            console.log('[loadTablePage] 响应结果:', response.result);
            console.log('[loadTablePage] tableSchema:', response.result.tableSchema);
            
            const tableName = APP_STATE.databases.tablePagination.currentTableName;
            const tableSchema = {
                tableName: tableName,
                columnInfo: response.result.tableSchema?.columnInfo || [],
                tableData: response.result.tableSchema?.tableData || {}
            };
            
            console.log('[loadTablePage] 构建的tableSchema:', {
                tableName: tableSchema.tableName,
                columnCount: tableSchema.columnInfo.length,
                hasTableData: !!tableSchema.tableData,
                tableDataKeys: Object.keys(tableSchema.tableData)
            });
            
            displayTablePreview(tableSchema);
        }
    } catch (error) {
        console.error('加载表页面失败:', error);
        showMessage('加载失败: ' + error.message, 'error');
    } finally {
        hideLoading();
    }
}

// 页面加载完成后初始化应用
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        // 延迟一点执行，确保所有脚本都已加载
        setTimeout(() => {
            initApp();
        }, 100);
    });
} else {
    // DOM已加载完成，延迟一点执行
    setTimeout(() => {
        initApp();
    }, 100);
}

// 【修复】页面完全加载后，强制隐藏loading遮罩（最后的安全措施）
window.addEventListener('load', () => {
    console.log('[window.load] 页面完全加载，强制隐藏loading遮罩');
    const overlay = document.getElementById('loading-overlay');
    if (overlay) {
        overlay.style.display = 'none';
        overlay.style.visibility = 'hidden';
        overlay.style.opacity = '0';
        overlay.style.pointerEvents = 'none';
    }
    if (typeof hideLoading === 'function') {
        hideLoading();
    }
});

// 会话隔离测试函数
window.testSessionIsolation = function() {
    console.log('=== 会话隔离测试 ===');

    console.log('1. Excel会话状态:');
    console.log('   - APP_STATE.excelSession:', APP_STATE.excelSession ? APP_STATE.excelSession.id : 'null');

    console.log('2. 数据库会话状态:');
    console.log('   - MySQL会话:', APP_STATE.databaseSessions.mysql ? APP_STATE.databaseSessions.mysql.id : 'null');
    console.log('   - SQLite会话:', APP_STATE.databaseSessions.sqlite ? APP_STATE.databaseSessions.sqlite.id : 'null');
    console.log('   - 当前数据库会话:', APP_STATE.databaseSessions.current ? APP_STATE.databaseSessions.current.id : 'null');

    console.log('3. 当前活动会话 (currentSession):');
    console.log('   - APP_STATE.currentSession:', APP_STATE.currentSession ? APP_STATE.currentSession.id : 'null');
    console.log('   - 会话类型:', APP_STATE.currentSession ? (APP_STATE.currentSession.type || 'unknown') : 'null');

    console.log('4. 当前页面:', APP_STATE.currentPage);

    // 检查会话隔离性
    const excelSessionId = APP_STATE.excelSession?.id;
    const mysqlSessionId = APP_STATE.databaseSessions.mysql?.id;
    const sqliteSessionId = APP_STATE.databaseSessions.sqlite?.id;

    const hasIsolation = (!excelSessionId || !mysqlSessionId || excelSessionId !== mysqlSessionId) &&
                        (!excelSessionId || !sqliteSessionId || excelSessionId !== sqliteSessionId) &&
                        (!mysqlSessionId || !sqliteSessionId || mysqlSessionId !== sqliteSessionId);

    console.log('5. 会话隔离检查:', hasIsolation ? '✓ 通过' : '✗ 失败');

    if (!hasIsolation) {
        console.warn('⚠ 警告：检测到会话ID重复，可能存在隔离问题');
    }

    console.log('=== 测试完成 ===');
    return hasIsolation;
};

// 【修复】额外的安全措施：定期检查并隐藏loading遮罩（防止某些异步操作导致遮罩一直显示）
setInterval(() => {
    const overlay = document.getElementById('loading-overlay');
    if (overlay && overlay.style.display === 'flex') {
        // 如果遮罩显示超过10秒，自动隐藏（防止卡住）
        const displayTime = overlay.dataset.displayTime || Date.now();
        if (Date.now() - parseInt(displayTime) > 10000) {
            console.warn('[定时检查] loading遮罩显示超过10秒，自动隐藏');
            hideLoading();
        }
    }
}, 5000);