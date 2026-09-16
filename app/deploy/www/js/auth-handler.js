// 认证处理器 - 处理index.html中的认证相关逻辑

// 退出登录处理
window.handleLogout = async function() {
    if (!confirm('确定要退出登录吗？')) {
        return;
    }
    
    try {
        await SessionManager.logout();
    } catch (error) {
        console.error('退出登录失败:', error);
        // 即使退出失败，也清除本地数据并跳转
        SessionManager.clearSessionId();
        SessionManager.clearUserInfo();
        window.location.href = 'login.html';
    }
};

// 加载用户信息
window.loadUserInfo = async function() {
    const sessionId = SessionManager.getSessionId();
    if (!sessionId) {
        console.warn('没有sessionId，无法加载用户信息');
        return;
    }
    
    // 尝试从本地缓存获取
    let userInfo = SessionManager.getUserInfo();
    
    if (!userInfo) {
        try {
            // 如果本地没有，从服务器获取
            const response = await AuthAPI.getUserInfo(sessionId, '');
            if (response.errorCode === 0 && response.result && response.result.userInfo) {
                userInfo = response.result.userInfo;
                SessionManager.saveUserInfo(userInfo);
            } else {
                console.warn('获取用户信息失败，会话可能已过期');
                return;
            }
        } catch (error) {
            console.error('获取用户信息失败:', error);
            return;
        }
    }
    
    // 更新页面显示
    if (userInfo) {
        // 更新头部用户昵称
        const userNicknameElem = document.getElementById('user-nickname');
        if (userNicknameElem) {
            userNicknameElem.textContent = userInfo.nickname || '未知用户';
        }
        
        // 更新个人中心的用户信息
        const profileNicknameElem = document.getElementById('profile-nickname');
        if (profileNicknameElem) {
            profileNicknameElem.textContent = userInfo.nickname || '未知用户';
        }
        
        const profileEmailElem = document.getElementById('profile-email');
        if (profileEmailElem) {
            profileEmailElem.textContent = userInfo.email || '未知邮箱';
        }
    }
};

// 更新个人中心的文件列表
window.updateProfileFilesList = function() {
    const filesList = document.getElementById('profile-files-list');
    if (!filesList) return;
    
    filesList.innerHTML = '';
    
    if (!APP_STATE.files || APP_STATE.files.length === 0) {
        filesList.innerHTML = '<div class="empty-state">暂无文件</div>';
        return;
    }
    
    APP_STATE.files.forEach(file => {
        const fileItem = document.createElement('div');
        fileItem.className = 'file-item';
        
        const fileName = file.fileName || file.name || '未知文件';
        const fileId = file.fileId || file.id;
        const fileSize = file.fileSize || file.size || 0;
        
        const uploadTime = file.uploadTime || 0;
        const uploadDate = new Date(uploadTime * 1000);
        const formattedTime = uploadDate.toLocaleString('zh-CN', {
            year: 'numeric',
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit'
        });
        
        fileItem.innerHTML = `
            <i class="fas fa-file-excel file-icon"></i>
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
};

// 扩展switchPage函数，在切换到个人中心时更新文件列表
(function() {
    // 等待main.js加载完成后再扩展
    const checkAndExtend = () => {
        if (typeof window.switchPage === 'function') {
            const originalSwitchPage = window.switchPage;
            window.switchPage = function(pageName) {
                originalSwitchPage(pageName);
                
                // 如果切换到个人中心页面，更新文件列表
                if (pageName === 'profile') {
                    updateProfileFilesList();
                }
            };
        } else {
            // 如果switchPage还未定义，100ms后重试
            setTimeout(checkAndExtend, 100);
        }
    };
    
    checkAndExtend();
})();

