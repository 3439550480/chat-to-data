// API接口封装

// 基础请求函数
async function apiRequest(url, options = {}) {
    const defaultOptions = {
        headers: {
            'Content-Type': 'application/json',
        },
        timeout: 30000
    };
    
    const mergedOptions = {
        ...defaultOptions,
        ...options,
        headers: {
            ...defaultOptions.headers,
            ...options.headers
        }
    };
    
    try {
        // 验证URL是否有效
        if (!url || typeof url !== 'string') {
            throw new Error('无效的URL: ' + url);
        }
        
        // 如果是相对路径，需要拼接基础URL
        let fullUrl = url;
        if (url.startsWith('/')) {
            // 相对路径，需要添加基础URL
            if (typeof CONFIG !== 'undefined' && CONFIG.API_BASE_URL) {
                fullUrl = CONFIG.API_BASE_URL + url;
            } else {
                // 如果没有CONFIG，使用当前页面的origin
                fullUrl = window.location.origin + url;
            }
        }
        
        // 创建新的URL对象，确保使用正确的服务器地址
        const urlObj = new URL(fullUrl);
        // 绕过代理设置，直接连接到目标服务器
        const response = await fetch(urlObj, {
            ...mergedOptions,
            // 强制使用目标服务器，不经过代理
            mode: 'cors',
            credentials: 'omit'
        });
        
        const contentType = response.headers.get('content-type');
        
        // 首先尝试解析JSON响应，无论状态码如何
        if (contentType && contentType.includes('application/json')) {
            const result = await response.json();
            
            // 统一处理API响应格式（使用errorCode和errorMsg）
            if (result.errorCode !== undefined) {
                const response = {
                    success: result.errorCode === 0,
                    message: result.errorMsg || '',
                };
                // 保留原有的数据字段（可能是result或data）
                if (result.result !== undefined) {
                    response.result = result.result;
                }
                if (result.data !== undefined) {
                    response.data = result.data;
                }
                // 保留其他可能的字段（如fileId, fileInfo等）
                if (result.fileId !== undefined) {
                    response.fileId = result.fileId;
                }
                if (result.fileInfo !== undefined) {
                    response.fileInfo = result.fileInfo;
                }
                return response;
            }
            
            // 处理模型服务API的错误响应格式
            if (result.success !== undefined) {
                return result;
            }
            
            // 如果响应不成功，但返回了JSON，则抛出包含错误信息的错误
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}, message: ${JSON.stringify(result)}`);
            }
            
            return result;
        } else if (!response.ok) {
            // 对于非JSON的错误响应，抛出通用错误
            throw new Error(`HTTP error! status: ${response.status}`);
        } else {
            // 对于成功的非JSON响应，返回文本
            return await response.text();
        }
    } catch (error) {
        console.error('API请求失败:', error);
        // 不显示错误消息，避免初始化时弹出多个错误提示
        // showMessage('网络请求失败，请检查网络连接', 'error');
        throw error;
    }
}

// 文件服务API
window.FileAPI = {
    // 检查服务状态
    async checkHealth() {
        const url = CONFIG.API_BASE_URL + CONFIG.FILE_SERVER.HEALTH;
        return await apiRequest(url, {
            method: 'GET'
        });
    },
    
    // 上传文件信息
    async uploadFileInfo(fileInfo) {
        const url = CONFIG.API_BASE_URL + CONFIG.FILE_SERVER.UPLOAD_FILE_INFO;
        const data = {
            requestId: APP_STATE.user.requestId,
            sessionId: SessionManager.getSessionId() || '',
            fileInfo: fileInfo
        };
        
        return await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 获取文件信息
    async getFileInfo(fileId) {
        const requestId = APP_STATE.user.requestId;
        const sessionId = SessionManager.getSessionId() || '';
        const url = `${CONFIG.API_BASE_URL}${CONFIG.FILE_SERVER.GET_FILE_INFO}?requestId=${requestId}&sessionId=${sessionId}&fileId=${fileId}`;
        
        return await apiRequest(url, {
            method: 'GET'
        });
    },
    
    // 上传文件数据
    async uploadFile(file, fileId) {
        const url = `${CONFIG.API_BASE_URL}${CONFIG.FILE_SERVER.UPLOAD_FILE}?requestId=${APP_STATE.user.requestId}&sessionId=${SessionManager.getSessionId() || ''}&fileId=${fileId}`;
        
        return await apiRequest(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/octet-stream'
            },
            body: file
        });
    },
    
    // 下载文件
    async downloadFile(fileId) {
        const url = `${CONFIG.API_BASE_URL}${CONFIG.FILE_SERVER.DOWNLOAD_FILE}?requestId=${APP_STATE.user.requestId}&sessionId=${SessionManager.getSessionId() || ''}&fileId=${fileId}`;
        
        // 验证URL并处理相对路径
        let fullUrl = url;
        if (url.startsWith('/')) {
            if (typeof CONFIG !== 'undefined' && CONFIG.API_BASE_URL) {
                fullUrl = CONFIG.API_BASE_URL + url;
            } else {
                fullUrl = window.location.origin + url;
            }
        }
        
        // 绕过代理设置，直接连接到目标服务器
        const response = await fetch(new URL(fullUrl), {
            mode: 'cors',
            credentials: 'omit'
        });
        if (!response.ok) {
            throw new Error('下载失败');
        }
        
        const blob = await response.blob();
        return {
            success: true,
            result: blob
        };
    },
    
    // 删除文件
    async deleteFile(fileId) {
        const requestId = APP_STATE.user.requestId;
        const sessionId = SessionManager.getSessionId() || '';
        const url = CONFIG.API_BASE_URL + CONFIG.FILE_SERVER.DELETE_FILE + fileId +
            `?requestId=${requestId}` + (sessionId ? `&sessionId=${encodeURIComponent(sessionId)}` : '');
        
        return await apiRequest(url, {
            method: 'DELETE'
        });
    },
    
    // 获取文件列表
    async getFileList() {
        const url = CONFIG.API_BASE_URL + CONFIG.FILE_SERVER.FILE_LISTS;
        const data = {
            requestId: APP_STATE.user.requestId,
            sessionId: SessionManager.getSessionId() || ''
        };
        
        return await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 预览Excel文件
    async previewExcel(fileId, pageNumber = 1, pageSize = 50) {
        const url = CONFIG.API_BASE_URL + CONFIG.FILE_SERVER.PREVIEW_EXCEL;
        const data = {
            requestId: APP_STATE.user.requestId,
            sessionId: SessionManager.getSessionId() || '',
            fileId: fileId,
            pageNumber: pageNumber,
            pageSize: pageSize
        };
        
        return await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 关联文件和会话
    async handleFileChatSessionMap(fileId, chatSessionId) {
        const url = CONFIG.API_BASE_URL + CONFIG.FILE_SERVER.HANDLE_FILE_CHAT_SESSION_MAP;
        const data = {
            requestId: APP_STATE.user.requestId,
            sessionId: SessionManager.getSessionId() || '',
            fileId: fileId,
            chatSessionId: chatSessionId
        };
        
        console.log('========== 发送 handleFileChatSessionMap 请求 ==========');
        console.log('请求URL:', url);
        console.log('请求数据:', data);
        console.log('fileId:', fileId);
        console.log('chatSessionId:', chatSessionId);
        
        try {
            const response = await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(data)
            });
            
            console.log('========== handleFileChatSessionMap 响应 ==========');
            console.log('响应结果:', response);
            console.log('响应success:', response ? response.success : '响应为空');
            
            return response;
        } catch (error) {
            console.error('========== handleFileChatSessionMap 请求失败 ==========');
            console.error('错误详情:', error);
            console.error('错误堆栈:', error.stack);
            throw error;
        }
    },
    
    // 上传SQLite文件
    async uploadSQLiteFile(file) {
        const requestId = APP_STATE.user.requestId;
        const sessionId = SessionManager.getSessionId() || '';
        const filename = encodeURIComponent(file.name);
        
        const url = `${CONFIG.API_BASE_URL}${CONFIG.FILE_SERVER.UPLOAD_SQLITE_FILE}?requestId=${requestId}&sessionId=${sessionId}&filename=${filename}`;
        
        return await apiRequest(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/octet-stream'
            },
            body: file
        });
    }
};

// 模型服务API
window.ModelAPI = {
    // 获取会话列表
    async getSessions() {
        const sessionId = SessionManager.getSessionId();
            const url = CONFIG.API_BASE_URL + CONFIG.MODEL_SERVICE.GET_SESSIONS;
            const data = {
                requestId: APP_STATE.user.requestId,
                sessionId: sessionId || ''
            };
            return await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(data)
            });
    },
    
    // 获取可用模型
    async getModels() {
            const url = CONFIG.API_BASE_URL + CONFIG.MODEL_SERVICE.GET_MODELS;
            const sessionId = SessionManager.getSessionId() || '';
            const data = {
                requestId: APP_STATE.user.requestId,
                sessionId: sessionId
            };
            return await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(data)
            });
    },
    
    // 创建新会话
    async createSession(model, sessionType = 'excel', dbConnectionInfo = null) {
        const url = CONFIG.API_BASE_URL + CONFIG.MODEL_SERVICE.CREATE_SESSION;
        let sessionId = SessionManager.getSessionId();
        
        // 如果本地没有session，尝试用已有登录态再登录一次，避免空session导致直接返回
        if (!sessionId) {
            console.warn('[ModelAPI.createSession] 本地无sessionId，尝试进行会话登录');
            const ok = await SessionManager.trySessionLogin();
            if (ok) {
                sessionId = SessionManager.getSessionId();
                console.log('[ModelAPI.createSession] 会话登录成功，获取sessionId:', sessionId);
            } else {
                console.error('[ModelAPI.createSession] 会话登录失败，无法创建会话，跳转登录页');
                window.location.href = 'login.html';
                return;
            }
        }
        
        const data = {
            requestId: APP_STATE.user.requestId,
            sessionId: sessionId,
            modelName: model,
            title: '',
            sessionType: sessionType
        };
        
        // 如果是数据库会话，添加数据库连接信息
        if (sessionType === 'database' && dbConnectionInfo) {
            data.dbConnectionInfo = JSON.stringify(dbConnectionInfo);
        }
        
        console.log('[ModelAPI.createSession] 请求URL:', url);
        console.log('[ModelAPI.createSession] 请求Body:', data);
        
        return await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 发送消息（流式响应）
    async sendMessage(chatSessionId, message, chatType = 'plain', fileId = null, dbConnectId = null, databaseName = null, tableName = null, onChunk, onComplete, onError) {
        // 【修复】验证chatSessionId不能为空，后端要求此字段必须存在
        if (!chatSessionId || chatSessionId.trim() === '') {
            const error = new Error('chatSessionId不能为空，请先创建会话');
            console.error('[ModelAPI.sendMessage]', error.message);
            if (onError) onError(error);
            return;
        }
        
        const url = CONFIG.API_BASE_URL + CONFIG.MODEL_SERVICE.SEND_MESSAGE;
        const httpSessionId = SessionManager.getSessionId();  // 获取HTTP会话ID
        
        // 【修复】验证httpSessionId也不能为空
        if (!httpSessionId || httpSessionId.trim() === '') {
            const error = new Error('HTTP会话ID不能为空，请先登录');
            console.error('[ModelAPI.sendMessage]', error.message);
            if (onError) onError(error);
            return;
        }
        
        const data = {
            requestId: APP_STATE.user.requestId,
            sessionId: httpSessionId,
            chatSessionId: chatSessionId,
            chatType: chatType,
            message: message
        };
        if (fileId) data.fileId = fileId;
        if (dbConnectId) data.dbConnectId = dbConnectId;
        if (databaseName) data.databaseName = databaseName;
        if (tableName) data.tableName = tableName;
        
        try {
            // 验证URL并处理相对路径
            let fullUrl = url;
            if (url.startsWith('/')) {
                if (typeof CONFIG !== 'undefined' && CONFIG.API_BASE_URL) {
                    fullUrl = CONFIG.API_BASE_URL + url;
                } else {
                    fullUrl = window.location.origin + url;
                }
            }
            
            // 绕过代理设置，直接连接到目标服务器
            console.log('[API] 准备发送请求:', {
                url: fullUrl,
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                bodySize: JSON.stringify(data).length,
                data: data
            });
            
            const response = await fetch(new URL(fullUrl), {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(data),
                mode: 'cors',
                credentials: 'omit'
            });
            
            console.log('[API] 收到响应:', {
                status: response.status,
                statusText: response.statusText,
                ok: response.ok,
                contentType: response.headers.get('content-type'),
                headers: Object.fromEntries(response.headers.entries())
            });
            
            if (!response.ok) {
                const errorText = await response.text().catch(() => '无法读取错误响应');
                console.error('[API] HTTP错误响应:', {
                    status: response.status,
                    statusText: response.statusText,
                    body: errorText
                });
                throw new Error(`HTTP error! status: ${response.status}, message: ${errorText}`);
            }
            
            // 【修复】检查响应内容类型，如果不是text/event-stream，可能是错误响应
            const contentType = response.headers.get('content-type') || '';
            if (!contentType.includes('text/event-stream') && !contentType.includes('text/plain')) {
                console.warn('[API] 响应内容类型不是text/event-stream:', contentType);
                // 尝试读取响应内容，可能是JSON错误
                try {
                    const errorText = await response.text();
                    console.error('[API] 非流式响应内容:', errorText);
                    if (errorText.includes('error') || errorText.includes('Error')) {
                        throw new Error(`后端返回错误: ${errorText}`);
                    }
                } catch (e) {
                    // 如果已经是错误，直接抛出
                    if (e.message.includes('后端返回错误')) {
                        throw e;
                    }
                }
            }
            
            const reader = response.body.getReader();
            const decoder = new TextDecoder();
            let buffer = '';
            let completed = false; // 添加标志防止重复调用onComplete
            let hasReceivedData = false; // 添加标志跟踪是否收到任何数据
            
            console.log('[API] 开始读取流式响应，chatSessionId:', chatSessionId);
            
            while (true) {
                const { done, value } = await reader.read();
                
                if (done) {
                    console.log('[API] 流读取完成，buffer剩余:', buffer, 'hasReceivedData:', hasReceivedData);
                    // 【修复】如果流立即完成且没有收到任何数据，说明后端可能没有收到请求
                    if (!hasReceivedData && buffer.trim() === '') {
                        console.error('[API] 流立即完成且未收到任何数据，后端可能未收到请求');
                        if (onError) {
                            onError(new Error('后端未响应：流立即完成且未收到任何数据，请检查网络连接和后端服务状态'));
                        }
                        return;
                    }
                    if (onComplete && !completed) {
                        completed = true;
                        onComplete();
                    }
                    break;
                }
                
                hasReceivedData = true; // 标记已收到数据
                buffer += decoder.decode(value, { stream: true });
                
                // 调试：显示接收到的原始数据
                console.log('[API] 接收到原始数据，长度:', value.length, '当前buffer长度:', buffer.length);
                
                const lines = buffer.split('\n');
                buffer = lines.pop() || '';
                
                console.log('[API] 分割后得到', lines.length, '行，buffer剩余:', buffer.length);
                
                for (const line of lines) {
                    if (line.startsWith('data: ')) {
                        const content = line.slice(6).trim();
                        
                        console.log('[API] 解析SSE行，原始行长度:', line.length, 'content长度:', content.length);
                        if (content.length > 0 && content.length < 100) {
                            console.log('[API] content内容:', content);
                        }
                        
                        if (content === '[DONE]') {
                            console.log('[API] 收到[DONE]信号');
                            if (onComplete && !completed) {
                                completed = true;
                                onComplete();
                            }
                            return;
                        }
                        
                        // 【修复】检测后端返回的错误消息格式：data: {"error": "..."}
                        if (content.startsWith('{') && content.includes('"error"')) {
                            try {
                                const errorObj = JSON.parse(content);
                                if (errorObj.error) {
                                    console.error('[API] 后端返回错误:', errorObj.error);
                                    if (onError) {
                                        onError(new Error(errorObj.error));
                                    }
                                    return; // 遇到错误，停止处理
                                }
                            } catch (e) {
                                // 如果不是有效的JSON错误格式，继续正常处理
                                console.warn('[API] 尝试解析错误消息失败:', e);
                            }
                        }
                        
                        if (content) {
                            // 【重要修复】不要反转义JSON中的转义序列！
                            // 原来的代码错误地将 \\n 替换为真实的换行符，导致JSON解析失败
                            // JSON字符串中的 \\n、\\r、\\t 等应该保持原样，由 JSON.parse() 处理
                            // 只处理双反斜杠的情况（\\\\ -> \\）已经不需要了，因为不做其他替换
                            
                            // 直接使用原始内容，不做任何替换
                            let processedContent = content;
                            
                            console.log('[API] 调用onChunk，内容长度:', processedContent.length);
                            if (onChunk) {
                                try {
                                    onChunk(processedContent);
                                } catch (chunkError) {
                                    console.error('[API] onChunk回调执行出错:', chunkError);
                                    // 不中断流式传输，继续处理后续数据
                                }
                            }
                        }
                    } else if (line.trim()) {
                        // 非空行但不是SSE格式
                        console.warn('[API] 收到非SSE格式的行:', line.substring(0, 50));
                    }
                }
            }
        } catch (error) {
            console.error('发送消息失败:', error);
            if (onError) onError(error);
        }
    },
    
    // 删除会话
    async deleteSession(sessionId) {
        const httpSessionId = SessionManager.getSessionId();
            const url = CONFIG.API_BASE_URL + CONFIG.MODEL_SERVICE.DELETE_SESSION;
            const data = {
                requestId: APP_STATE.user.requestId,
                sessionId: httpSessionId || '',
                chatSessionId: sessionId
            };
            return await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(data)
            });
    },
    
    // 获取会话历史
    async getSessionHistory(sessionId) {
        const httpSessionId = SessionManager.getSessionId();
            const url = CONFIG.API_BASE_URL + CONFIG.MODEL_SERVICE.GET_SESSION_HISTORY;
            const data = {
                requestId: APP_STATE.user.requestId,
                sessionId: httpSessionId || '',
                chatSessionId: sessionId
            };
            return await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(data)
            });
    }
};

// 初始化API
async function initAPI() {
    try {
        // 生成请求ID（如果尚未生成）
        if (!APP_STATE.user.requestId) {
            APP_STATE.user.requestId = generateRequestId();
            console.log('生成的requestId:', APP_STATE.user.requestId);
        } else {
            console.log('使用已有的requestId:', APP_STATE.user.requestId);
        }
        
        // 【修复】不再自动创建默认会话，用户必须通过"新建会话"按钮创建会话
        // 这样可以确保会话ID在后端真实存在，避免发送消息时出现"会话不存在"的错误
        // if (!APP_STATE.currentSession.id) {
        //     APP_STATE.currentSession = {
        //         id: generateSessionId(),
        //         model: CONFIG.DEFAULTS.MODEL,
        //         createdAt: new Date().toISOString(),
        //         messages: []
        //     };
        //     console.log('创建默认会话:', APP_STATE.currentSession.id);
        // }
        
        // 检查文件服务状态
        try {
            await FileAPI.checkHealth();
            APP_STATE.ui.isConnected = true;
            console.log('文件服务连接成功');
        } catch (error) {
            console.warn('文件服务连接失败:', error);
            showMessage('文件服务连接失败，部分功能可能无法使用', 'warning');
        }
        
        // 获取模型列表（模型服务可能未运行，单独处理）
        try {
            const modelsResponse = await ModelAPI.getModels();
            if (modelsResponse && modelsResponse.success && modelsResponse.result) {
                APP_STATE.models = (modelsResponse.result.modelList || []).map(m => ({
                    modelName: m.modelName,
                    modelDesc: m.modelDesc
                }));
                updateModelSelect();
            }
        } catch (error) {
            console.warn('获取模型列表失败:', error);
            APP_STATE.models = [{ modelName: 'deepseek', modelDesc: 'DeepSeek模型' }];
            updateModelSelect();
        }
        
        // 注意：会话列表和文件列表改为懒加载
        // 只有在用户点击对应的导航菜单时才会获取
        // 见 main.js 中的 switchPage() 函数
        
        // 初始化空列表
        APP_STATE.sessions = [];
        APP_STATE.files = [];
        
    } catch (error) {
        console.error('API初始化失败:', error);
        showMessage('服务连接失败，部分功能可能无法使用', 'warning');
    }
}

// 更新模型选择下拉框
function updateModelSelect() {
    const modelSelect = document.getElementById('model-select');
    if (!modelSelect) return;
    
    modelSelect.innerHTML = '';
    
    APP_STATE.models.forEach(model => {
        const option = document.createElement('option');
        const name = model.modelName || model.name;
        option.value = name;
        option.textContent = name;
        if (name === (CONFIG.DEFAULTS && CONFIG.DEFAULTS.MODEL)) {
            option.selected = true;
        }
        
        modelSelect.appendChild(option);
    });
}

// 更新会话列表
function updateSessionsList() {
    const sessionsList = document.getElementById('sessions-list');
    if (!sessionsList) return;
    
    sessionsList.innerHTML = '';
    
    APP_STATE.sessions.forEach(session => {
        const sessionItem = document.createElement('div');
        sessionItem.className = 'session-item';
        sessionItem.innerHTML = `
            <div class="session-header">
                <span class="session-model">${session.model}</span>
                <span class="session-time">${formatTime(session.created_at)}</span>
            </div>
            <div class="session-preview">${session.first_user_message || '无消息'}</div>
        `;
        
        sessionItem.addEventListener('click', () => {
            // 切换到该会话
            switchToSession(session.id);
        });
        
        sessionsList.appendChild(sessionItem);
    });
}

// 更新文件列表
function updateFilesList() {
    const filesList = document.getElementById('files-list');
    if (!filesList) return;
    
    filesList.innerHTML = '';
    
    APP_STATE.files.forEach(file => {
        const fileItem = document.createElement('div');
        fileItem.className = 'file-item';
        fileItem.innerHTML = `
            <i class="fas fa-file-excel file-icon"></i>
            <div class="file-info">
                <h4>${file.fileName}</h4>
                <p>文件ID: ${file.fileId}</p>
            </div>
        `;
        
        fileItem.addEventListener('click', () => {
            // 预览文件
            previewFile(file.fileId);
        });
        
        filesList.appendChild(fileItem);
    });
}

// 切换到指定会话
async function switchToSession(sessionId) {
    try {
        showLoading('加载会话中...');
        
        // 获取会话历史
        const historyResponse = await ModelAPI.getSessionHistory(sessionId);
        
        if (historyResponse.success) {
            // 更新当前会话状态
            const session = APP_STATE.sessions.find(s => s.id === sessionId);
            if (session) {
                APP_STATE.currentSession = {
                    id: sessionId,
                    model: session.model,
                    createdAt: session.created_at,
                    messages: historyResponse.data || []
                };
                
                // 更新界面
                updateChatMessages();
                updateModelSelect();
                
                // 启用发送按钮
                document.getElementById('send-btn').disabled = false;
                
                // 切换到聊天页面
                switchPage('chat');
            }
        }
        
    } catch (error) {
        console.error('切换会话失败:', error);
        showMessage('加载会话失败', 'error');
    } finally {
        hideLoading();
    }
}

// 预览文件
async function previewFile(fileId) {
    try {
        showLoading('加载文件中...');
        
        const previewResponse = await FileAPI.previewExcel(fileId);
        
        if (previewResponse.success) {
            // 显示文件预览
            displayExcelPreview(previewResponse.result);
            
            // 切换到文件预览标签页
            switchTab('file-preview');
            
            // 切换到聊天页面
            switchPage('chat');
        }
        
    } catch (error) {
        console.error('预览文件失败:', error);
        showMessage('预览文件失败', 'error');
    } finally {
        hideLoading();
    }
}

// 页面加载完成后初始化API
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initAPI);
} else {
    initAPI();
}