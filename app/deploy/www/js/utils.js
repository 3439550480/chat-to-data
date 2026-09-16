// 工具函数集合

// Unicode转码函数
function unicodeToChar(text) {
    return text.replace(/\\u[0-9a-f]{4}/gi, function(match) {
        return String.fromCharCode(parseInt(match.replace(/\\u/g, ''), 16));
    });
}

// 添加Markdown样式
function addMarkdownStyles() {
    if (document.getElementById('markdown-styles')) return;
    
    const style = document.createElement('style');
    style.id = 'markdown-styles';
    style.textContent = `
        .markdown-content {
            line-height: 1.6;
            color: #333;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
        }
        
        .markdown-content h1, .markdown-content h2, .markdown-content h3,
        .markdown-content h4, .markdown-content h5, .markdown-content h6 {
            margin: 1.5em 0 0.5em 0;
            font-weight: 600;
            line-height: 1.25;
        }
        
        .markdown-content h1 { font-size: 2em; border-bottom: 1px solid #eaecef; padding-bottom: 0.3em; }
        .markdown-content h2 { font-size: 1.5em; border-bottom: 1px solid #eaecef; padding-bottom: 0.3em; }
        .markdown-content h3 { font-size: 1.25em; }
        .markdown-content h4 { font-size: 1em; }
        .markdown-content h5 { font-size: 0.875em; }
        .markdown-content h6 { font-size: 0.85em; color: #6a737d; }
        
        .markdown-content p {
            margin: 1em 0;
        }
        
        .markdown-content ul, .markdown-content ol {
            padding-left: 2em;
            margin: 1em 0;
        }
        
        .markdown-content li {
            margin: 0.25em 0;
        }
        
        .markdown-content blockquote {
            margin: 1em 0;
            padding: 0 1em;
            border-left: 4px solid #dfe2e5;
            background-color: #f6f8fa;
            color: #6a737d;
        }
        
        .markdown-content code {
            background-color: #f6f8fa;
            padding: 0.2em 0.4em;
            border-radius: 3px;
            font-family: 'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
            font-size: 0.85em;
        }
        
        .markdown-content pre {
            background-color: #f6f8fa;
            border-radius: 6px;
            padding: 1em;
            overflow: auto;
            margin: 1em 0;
        }
        
        .markdown-content pre code {
            background: none;
            padding: 0;
            border-radius: 0;
        }
        
        .markdown-content table {
            border-collapse: collapse;
            width: 100%;
            margin: 1em 0;
        }
        
        .markdown-content table th,
        .markdown-content table td {
            border: 1px solid #dfe2e5;
            padding: 0.5em 1em;
            text-align: left;
        }
        
        .markdown-content table th {
            background-color: #f6f8fa;
            font-weight: 600;
        }
        
        .markdown-content a {
            color: #0366d6;
            text-decoration: none;
        }
        
        .markdown-content a:hover {
            text-decoration: underline;
        }
        
        .markdown-content img {
            max-width: 100%;
            height: auto;
            border-radius: 4px;
            margin: 1em 0;
        }
        
        .markdown-content hr {
            height: 1px;
            background-color: #e1e4e8;
            border: none;
            margin: 2em 0;
        }
        
        .markdown-content del {
            text-decoration: line-through;
            color: #6a737d;
        }
        
        .markdown-content strong {
            font-weight: 600;
        }
        
        .markdown-content em {
            font-style: italic;
        }
    `;
    
    document.head.appendChild(style);
}

// Markdown解析器（增强版）
function parseMarkdown(text) {
    if (!text) return '';
    
    // 首先处理多余的双引号：将连续的双引号替换为单个双引号
    // 同时处理转义的双引号
    text = text.replace(/"{2,}/g, '"').replace(/\\\\\\"/g, '"').replace(/\\"/g, '"');
    
    // 转义HTML
    text = text.replace(/&/g, '&amp;')
               .replace(/</g, '&lt;')
               .replace(/>/g, '&gt;')
               .replace(/"/g, '&quot;')
               .replace(/'/g, '&#039;');
    
    // 标题（支持1-6级）
    text = text.replace(/^###### (.*$)/gim, '<h6>$1</h6>')
               .replace(/^##### (.*$)/gim, '<h5>$1</h5>')
               .replace(/^#### (.*$)/gim, '<h4>$1</h4>')
               .replace(/^### (.*$)/gim, '<h3>$1</h3>')
               .replace(/^## (.*$)/gim, '<h2>$1</h2>')
               .replace(/^# (.*$)/gim, '<h1>$1</h1>');
    
    // 粗体
    text = text.replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>')
               .replace(/__(.*?)__/g, '<strong>$1</strong>');
    
    // 斜体
    text = text.replace(/\*(.*?)\*/g, '<em>$1</em>')
               .replace(/_(.*?)_/g, '<em>$1</em>');
    
    // 删除线
    text = text.replace(/~~(.*?)~~/g, '<del>$1</del>');
    
    // 代码块（支持语法高亮）
    text = text.replace(/```(\w+)?\n([\s\S]*?)\n```/g, function(match, lang, code) {
        return '<pre><code class="' + (lang || '') + '">' + code + '</code></pre>';
    });
    
    // 行内代码
    text = text.replace(/`(.*?)`/g, '<code>$1</code>');
    
    // 引用块
    text = text.replace(/^> (.*$)/gim, '<blockquote>$1</blockquote>');
    
    // 链接
    text = text.replace(/\[([^\]]+)\]\(([^\)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
    
    // 图片
    text = text.replace(/!\[([^\]]*)\]\(([^\)]+)\)/g, '<img src="$2" alt="$1" style="max-width: 100%; height: auto;">');
    
    // 无序列表
    text = text.replace(/^\* (.*$)/gim, '<li>$1</li>');
    text = text.replace(/^- (.*$)/gim, '<li>$1</li>');
    text = text.replace(/^\+ (.*$)/gim, '<li>$1</li>');
    
    // 有序列表
    text = text.replace(/^\d+\. (.*$)/gim, '<li>$1</li>');
    
    // 包装列表
    text = text.replace(/(<li>.*<\/li>)/gim, function(match) {
        if (match.match(/^<li>\d+\./)) {
            return '<ol>' + match + '</ol>';
        } else {
            return '<ul>' + match + '</ul>';
        }
    });
    
    // 水平分割线
    text = text.replace(/^---$/gim, '<hr>');
    text = text.replace(/^\*\*\*$/gim, '<hr>');
    text = text.replace(/^___$/gim, '<hr>');
    
    // 表格（基本支持）
    text = text.replace(/\|(.+)\|\n\|[-:| ]+\|\n((?:\|.+\|\n)*)/g, function(match, header, rows) {
        let table = '<table border="1" style="border-collapse: collapse; width: 100%;"><thead><tr>';
        header.split('|').forEach(cell => {
            table += '<th style="padding: 8px; border: 1px solid #ddd;">' + cell.trim() + '</th>';
        });
        table += '</tr></thead><tbody>';
        
        rows.split('\n').forEach(row => {
            if (row.trim()) {
                table += '<tr>';
                row.split('|').forEach(cell => {
                    table += '<td style="padding: 8px; border: 1px solid #ddd;">' + cell.trim() + '</td>';
                });
                table += '</tr>';
            }
        });
        table += '</tbody></table>';
        return table;
    });
    
    // 换行处理
    text = text.replace(/\n\n/g, '</p><p>');
    text = text.replace(/\n/g, '<br>');
    
    // 段落包装
    text = '<p>' + text + '</p>';
    
    // 清理多余的段落标签
    text = text.replace(/<p><\/p>/g, '');
    text = text.replace(/<p><(h[1-6]|blockquote|ul|ol|table|pre)>/, '<$1>');
    text = text.replace(/<\/(h[1-6]|blockquote|ul|ol|table|pre)><\/p>/, '</$1>');
    
    // 添加markdown-content类
    text = '<div class="markdown-content">' + text + '</div>';
    
    return text;
}

// 格式化文件大小
function formatFileSize(bytes) {
    if (bytes === 0) return '0 Bytes';
    
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// 格式化时间
function formatTime(timestamp) {
    const date = new Date(timestamp);
    const now = new Date();
    const diff = now - date;
    
    if (diff < 60000) { // 1分钟内
        return '刚刚';
    } else if (diff < 3600000) { // 1小时内
        return Math.floor(diff / 60000) + '分钟前';
    } else if (diff < 86400000) { // 1天内
        return Math.floor(diff / 3600000) + '小时前';
    } else if (diff < 604800000) { // 1周内
        return Math.floor(diff / 86400000) + '天前';
    } else {
        return date.toLocaleDateString('zh-CN');
    }
}

// 防抖函数
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

// 节流函数
function throttle(func, limit) {
    let inThrottle;
    return function() {
        const args = arguments;
        const context = this;
        if (!inThrottle) {
            func.apply(context, args);
            inThrottle = true;
            setTimeout(() => inThrottle = false, limit);
        }
    };
}

// 验证文件类型
function validateFileType(file) {
    const allowedExtensions = CONFIG.UPLOAD.ALLOWED_EXTENSIONS;
    const fileExtension = '.' + file.name.split('.').pop().toLowerCase();
    return allowedExtensions.includes(fileExtension);
}

// 验证文件大小
function validateFileSize(file) {
    return file.size <= CONFIG.UPLOAD.MAX_FILE_SIZE;
}

// 显示消息提示
function showMessage(message, type = 'info') {
    // 创建消息元素
    const messageEl = document.createElement('div');
    messageEl.className = `message-toast ${type}`;
    messageEl.innerHTML = `
        <i class="fas ${getMessageIcon(type)}"></i>
        <span>${message}</span>
    `;
    
    // 添加样式
    messageEl.style.cssText = `
        position: fixed;
        top: 100px;
        right: 20px;
        background: ${getMessageColor(type)};
        color: white;
        padding: 1rem 1.5rem;
        border-radius: 8px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        z-index: 3000;
        display: flex;
        align-items: center;
        gap: 0.5rem;
        max-width: 400px;
        animation: slideInRight 0.3s ease;
    `;
    
    // 添加到页面
    document.body.appendChild(messageEl);
    
    // 3秒后自动移除
    setTimeout(() => {
        messageEl.style.animation = 'slideOutRight 0.3s ease';
        setTimeout(() => {
            if (messageEl.parentNode) {
                messageEl.parentNode.removeChild(messageEl);
            }
        }, 300);
    }, 3000);
}

// 获取消息图标
function getMessageIcon(type) {
    switch (type) {
        case 'success': return 'fa-check-circle';
        case 'error': return 'fa-exclamation-circle';
        case 'warning': return 'fa-exclamation-triangle';
        default: return 'fa-info-circle';
    }
}

// 获取消息颜色
function getMessageColor(type) {
    switch (type) {
        case 'success': return '#28a745';
        case 'error': return '#dc3545';
        case 'warning': return '#ffc107';
        default: return '#17a2b8';
    }
}

// 显示加载遮罩
function showLoading(message = '处理中...') {
    const overlay = document.getElementById('loading-overlay');
    if (!overlay) {
        console.warn('loading-overlay元素不存在，无法显示加载遮罩');
        return;
    }
    const spinner = overlay.querySelector('.loading-spinner p');
    
    if (spinner) {
        spinner.textContent = message;
    }
    // 【修复】确保遮罩正确显示，并记录显示时间
    overlay.style.display = 'flex';
    overlay.style.visibility = 'visible';
    overlay.style.opacity = '1';
    overlay.style.pointerEvents = 'auto';
    overlay.dataset.displayTime = Date.now().toString();
    console.log('[showLoading] loading遮罩已显示:', message);
}

// 隐藏加载遮罩
function hideLoading() {
    const overlay = document.getElementById('loading-overlay');
    if (overlay) {
        overlay.style.display = 'none';
        // 【修复】强制设置样式，确保遮罩被隐藏
        overlay.style.visibility = 'hidden';
        overlay.style.opacity = '0';
        overlay.style.pointerEvents = 'none';
        console.log('[hideLoading] loading遮罩已隐藏');
    } else {
        console.warn('[hideLoading] loading-overlay元素不存在');
    }
}

// 滚动到底部
function scrollToBottom(element) {
    if (element) {
        element.scrollTop = element.scrollHeight;
    }
}

// 检查网络连接
function checkNetworkConnection() {
    return navigator.onLine;
}

// 添加CSS动画
function addCSSAnimations() {
    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideInRight {
            from {
                transform: translateX(100%);
                opacity: 0;
            }
            to {
                transform: translateX(0);
                opacity: 1;
            }
        }
        
        @keyframes slideOutRight {
            from {
                transform: translateX(0);
                opacity: 1;
            }
            to {
                transform: translateX(100%);
                opacity: 0;
            }
        }
        
        @keyframes fadeIn {
            from { opacity: 0; }
            to { opacity: 1; }
        }
        
        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.05); }
            100% { transform: scale(1); }
        }
    `;
    document.head.appendChild(style);
}

// 初始化工具函数
function initUtils() {
    addCSSAnimations();
    
    // 初始化用户ID和请求ID
    if (!APP_STATE.user.id) {
        APP_STATE.user.id = generateUserId();
    }
    
    if (!APP_STATE.user.requestId) {
        APP_STATE.user.requestId = generateRequestId();
    }
}

// 页面加载完成后初始化
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initUtils);
} else {
    initUtils();
}