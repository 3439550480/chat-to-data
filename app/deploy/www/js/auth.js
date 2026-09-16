// 用户认证相关API封装

// 基础API请求函数（如果api.js中的不适用）
async function authApiRequest(url, options = {}) {
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
        // 验证URL并处理相对路径
        if (!url || typeof url !== 'string') {
            throw new Error('无效的URL: ' + url);
        }
        
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
        
        console.log('[authApiRequest] 发送请求到:', fullUrl);
        console.log('[authApiRequest] 请求选项:', {
            method: mergedOptions.method,
            headers: mergedOptions.headers,
            body: mergedOptions.body
        });
        
        const response = await fetch(fullUrl, {
            ...mergedOptions,
            mode: 'cors',
            credentials: 'omit'
        });
        
        console.log('[authApiRequest] 收到响应，状态码:', response.status);
        
        const contentType = response.headers.get('content-type');
        
        if (contentType && contentType.includes('application/json')) {
            const result = await response.json();
            console.log('[authApiRequest] 响应数据:', result);
            // 直接返回原始响应，不做包装
            return result;
        } else if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        } else {
            const text = await response.text();
            console.log('[authApiRequest] 响应文本:', text);
            return text;
        }
    } catch (error) {
        console.error('[authApiRequest] API请求失败:', error);
        throw error;
    }
}

// 用户认证API
window.AuthAPI = {
    // 1. 昵称检测
    async validNickname(nickname) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.VALID_NICKNAME;
        const data = {
            requestId: generateRequestId(),
            nickname: nickname
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 2. 邮箱检测
    async validEmail(email) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.VALID_EMAIL;
        const data = {
            requestId: generateRequestId(),
            email: email
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 3. 用户注册
    async userRegister(nickname, password, email) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.USER_REGISTER;
        const data = {
            requestId: generateRequestId(),
            nickname: nickname,
            password: password,
            email: email
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 4. 会话登录
    async sessionLogin(sessionId) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.SESSION_LOGIN;
        const data = {
            requestId: generateRequestId(),
            sessionId: sessionId
        };
        
        console.log('[AuthAPI] sessionLogin - URL:', url);
        console.log('[AuthAPI] sessionLogin - 请求数据:', data);
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 5. 密码登录
    async passwdLogin(username, password) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.PASSWD_LOGIN;
        const data = {
            requestId: generateRequestId(),
            username: username,
            password: password
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 6. 获取验证码
    async getCode(email) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.GET_CODE;
        const data = {
            requestId: generateRequestId(),
            email: email
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 7. 邮箱登录
    async vcodeLogin(email, verifyCode, codeId) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.VCODE_LOGIN;
        const data = {
            requestId: generateRequestId(),
            email: email,
            verifyCode: verifyCode,
            codeId: codeId
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 8. 退出登录
    async logout(sessionId) {
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.LOGOUT;
        const data = {
            requestId: generateRequestId(),
            sessionId: sessionId
        };
        
        return await authApiRequest(url, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    },
    
    // 9. 获取用户信息
    async getUserInfo(sessionId, userId) {
        const requestId = generateRequestId();
        // 构建URL参数
        const params = new URLSearchParams({
            requestId: requestId,
            sessionId: sessionId || '',
            userId: userId || ''
        });
        
        const url = CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.GET_USER_INFO + '?' + params.toString();
        
        return await authApiRequest(url, {
            method: 'GET'
        });
    }
};

// 会话管理
window.SessionManager = {
    // 防抖相关变量
    _trySessionLoginPromise: null,
    _lastTrySessionLoginTime: 0,
    _lastDebounceDelay: 5000, // 上次使用的防抖延迟时间
    _trySessionLoginDebounceMs: 5000, // 5秒内不重复调用（正常情况）
    _trySessionLoginDebounceMsError: 10000, // 10秒内不重复调用（错误情况，如500）
    
    // 保存会话ID
    saveSessionId(sessionId) {
        localStorage.setItem('sessionId', sessionId);
        // 清除防抖状态，允许新的登录尝试
        this._trySessionLoginPromise = null;
        this._lastTrySessionLoginTime = 0;
        this._lastDebounceDelay = this._trySessionLoginDebounceMs; // 重置为默认防抖时间
    },
    
    // 获取会话ID
    getSessionId() {
        return localStorage.getItem('sessionId');
    },
    
    // 清除会话ID
    clearSessionId() {
        localStorage.removeItem('sessionId');
        // 清除防抖状态
        this._trySessionLoginPromise = null;
        this._lastTrySessionLoginTime = 0;
        this._lastDebounceDelay = this._trySessionLoginDebounceMs; // 重置为默认防抖时间
    },
    
    // 保存用户信息
    saveUserInfo(userInfo) {
        localStorage.setItem('userInfo', JSON.stringify(userInfo));
    },
    
    // 获取用户信息
    getUserInfo() {
        const userInfo = localStorage.getItem('userInfo');
        return userInfo ? JSON.parse(userInfo) : null;
    },
    
    // 清除用户信息
    clearUserInfo() {
        localStorage.removeItem('userInfo');
    },
    
    // 检查是否已登录
    isLoggedIn() {
        return !!this.getSessionId();
    },
    
    // 尝试会话登录（带防抖机制）
    async trySessionLogin() {
        const sessionId = this.getSessionId();
        console.log('[SessionManager] trySessionLogin - 开始');
        console.log('[SessionManager] trySessionLogin - sessionId:', sessionId);
        
        if (!sessionId) {
            console.log('[SessionManager] trySessionLogin - 没有sessionId，返回false（不会发送请求）');
            return false;
        }
        
        // 防抖检查：如果最近防抖时间内已经调用过，直接返回之前的结果
        const now = Date.now();
        const debounceTime = this._lastDebounceDelay; // 使用上次的防抖时间
        if (this._trySessionLoginPromise && (now - this._lastTrySessionLoginTime) < debounceTime) {
            console.log('[SessionManager] trySessionLogin - 防抖：最近已调用过（', Math.round((debounceTime - (now - this._lastTrySessionLoginTime)) / 1000), '秒后允许），返回之前的Promise');
            return this._trySessionLoginPromise;
        }
        
        // 记录调用时间
        this._lastTrySessionLoginTime = now;
        
        // 创建新的Promise
        this._trySessionLoginPromise = (async () => {
            let shouldUseLongDebounce = false; // 标记是否应该使用长防抖时间（错误情况）
            
            try {
                console.log('[SessionManager] trySessionLogin - 开始调用AuthAPI.sessionLogin');
                console.log('[SessionManager] trySessionLogin - 请求URL:', CONFIG.API_BASE_URL + CONFIG.USER_SERVICE.SESSION_LOGIN);
                console.log('[SessionManager] trySessionLogin - 请求数据:', { requestId: 'will be generated', sessionId: sessionId });
                
                const response = await AuthAPI.sessionLogin(sessionId);
                console.log('[SessionManager] trySessionLogin - 收到响应:', response);
                console.log('[SessionManager] trySessionLogin - 响应类型:', typeof response);
                console.log('[SessionManager] trySessionLogin - 响应errorCode:', response?.errorCode);
                
                // 根据API文档，errorCode为0表示成功
                if (response && response.errorCode === 0) {
                    console.log('[SessionManager] trySessionLogin - ✓ 登录成功');
                    return true;
                } else {
                    console.log('[SessionManager] trySessionLogin - ✗ 登录失败，errorCode:', response?.errorCode);
                    console.log('[SessionManager] trySessionLogin - 错误信息:', response?.errorMsg);
                    
                    // 根据错误码判断是否应该清除sessionId
                    // 500错误通常表示服务器内部错误，不应该清除sessionId（可能是服务暂时不可用）
                    // 401/403等认证错误才应该清除sessionId
                    if (response && response.errorCode === 500) {
                        console.warn('[SessionManager] trySessionLogin - 服务器错误(500)，保留sessionId以便稍后重试');
                        shouldUseLongDebounce = true;
                        return false;
                    }
                    
                    // 其他错误（如会话无效、认证失败等），清除sessionId
                    console.warn('[SessionManager] trySessionLogin - 会话无效或认证失败，清除sessionId');
                    this.clearSessionId();
                    this.clearUserInfo();
                    return false;
                }
            } catch (error) {
                console.error('[SessionManager] trySessionLogin - ✗ 异常:', error);
                console.error('[SessionManager] trySessionLogin - 异常类型:', error?.constructor?.name);
                console.error('[SessionManager] trySessionLogin - 异常消息:', error?.message);
                console.error('[SessionManager] trySessionLogin - 异常堆栈:', error?.stack);
                
                // 网络错误不清除sessionId，使用长防抖时间
                if (error.name === 'TypeError' || error.message?.includes('fetch')) {
                    console.warn('[SessionManager] trySessionLogin - 网络错误，保留sessionId以便稍后重试，使用长防抖时间');
                    shouldUseLongDebounce = true;
                    return false;
                }
                
                // 其他异常，清除本地会话
                this.clearSessionId();
                this.clearUserInfo();
                return false;
            } finally {
                // 根据错误类型选择防抖时间：错误情况使用更长的防抖时间
                const debounceDelay = shouldUseLongDebounce ? this._trySessionLoginDebounceMsError : this._trySessionLoginDebounceMs;
                this._lastDebounceDelay = debounceDelay; // 记录本次使用的防抖时间
                console.log('[SessionManager] trySessionLogin - 将在', debounceDelay / 1000, '秒后允许下次调用');
                
                // 清除Promise引用，允许下次调用
                setTimeout(() => {
                    this._trySessionLoginPromise = null;
                    console.log('[SessionManager] trySessionLogin - 防抖时间已过，允许下次调用');
                }, debounceDelay);
            }
        })();
        
        return this._trySessionLoginPromise;
    },
    
    // 退出登录
    async logout() {
        const sessionId = this.getSessionId();
        if (sessionId) {
            try {
                await AuthAPI.logout(sessionId);
            } catch (error) {
                console.error('退出登录失败:', error);
            }
        }
        
        this.clearSessionId();
        this.clearUserInfo();
        
        // 跳转到登录页面
        window.location.href = 'login.html';
    }
};

// 密码验证工具
window.PasswordValidator = {
    // 检查密码强度
    validate(password) {
        if (!password || password.length < 8) {
            return {
                valid: false,
                message: '密码长度不能少于8位'
            };
        }
        
        // 计算包含的字符类型数量
        let types = 0;
        if (/[0-9]/.test(password)) types++; // 数字
        if (/[a-z]/.test(password)) types++; // 小写字母
        if (/[A-Z]/.test(password)) types++; // 大写字母
        if (/[^0-9a-zA-Z]/.test(password)) types++; // 特殊字符
        
        if (types < 2) {
            return {
                valid: false,
                message: '密码必须包含数字、大小写字母、特殊字符中的至少两种'
            };
        }
        
        return {
            valid: true,
            message: '密码强度符合要求'
        };
    },
    
    // 获取密码强度等级
    getStrength(password) {
        if (!password) return 0;
        
        let strength = 0;
        if (password.length >= 8) strength++;
        if (password.length >= 12) strength++;
        if (/[0-9]/.test(password)) strength++;
        if (/[a-z]/.test(password)) strength++;
        if (/[A-Z]/.test(password)) strength++;
        if (/[^0-9a-zA-Z]/.test(password)) strength++;
        
        return Math.min(strength, 5);
    }
};

// 邮箱验证工具
window.EmailValidator = {
    // 验证邮箱格式
    validate(email) {
        const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
        return emailRegex.test(email);
    },
    
    // 获取验证结果
    getValidationResult(email) {
        if (!email) {
            return {
                valid: false,
                message: '请输入邮箱地址'
            };
        }
        
        if (!this.validate(email)) {
            return {
                valid: false,
                message: '邮箱格式不正确'
            };
        }
        
        return {
            valid: true,
            message: '邮箱格式正确'
        };
    }
};

