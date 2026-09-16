// 聊天功能模块

// 使用全局safeJSONParse函数（由main.js定义）
// 如果main.js还未加载，则创建一个临时实现
if (typeof window.safeJSONParse === 'undefined') {
    console.warn('[chat.js] safeJSONParse未定义，请确保main.js已加载');
    // 临时实现，如果main.js未加载
    window.safeJSONParse = function(jsonStr) {
        try {
            return JSON.parse(jsonStr.trim());
        } catch (e) {
            console.error('[safeJSONParse] JSON解析失败（临时实现）:', e.message);
            return null;
        }
    };
}

// 创建本地引用（使用var避免重复声明错误）
var safeJSONParse = window.safeJSONParse;

// Unicode解码辅助函数
function decodeUnicodeString(str) {
    if (!str) return '';
    
    try {
        // 方法1：使用JSON.parse解码unicode
        // 将字符串包装成JSON字符串格式，然后解析
        const jsonStr = '"' + str.replace(/"/g, '\\"') + '"';
        return JSON.parse(jsonStr);
    } catch (e) {
        console.warn('[Unicode解码] 方法1失败，尝试方法2:', e.message);
        
        try {
            // 方法2：手动替换\uXXXX格式
            return str.replace(/\\u([\dA-Fa-f]{4})/g, (match, grp) => {
                return String.fromCharCode(parseInt(grp, 16));
            });
        } catch (e2) {
            console.error('[Unicode解码] 方法2也失败:', e2.message);
            return str; // 返回原始字符串
        }
    }
}

// 流式动画：逐字符显示文本
function typewriterEffect(element, fullText, speed = 20, onComplete = null) {
    if (!element || !fullText) {
        if (onComplete) onComplete();
        return;
    }
    
    const escapedText = fullText
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
    
    let index = 0;
    element.innerHTML = '';
    
    function typeNextChar() {
        if (index < escapedText.length) {
            element.innerHTML += escapedText[index];
            index++;
            setTimeout(typeNextChar, speed);
        } else {
            if (onComplete) onComplete();
        }
    }
    
    typeNextChar();
}

// 流式动画：逐条显示列表项
function animateListItems(container, items, delay = 100, onComplete = null) {
    if (!container || !items || items.length === 0) {
        if (onComplete) onComplete();
        return;
    }
    
    let index = 0;
    
    function showNextItem() {
        if (index < items.length) {
            const item = items[index];
            item.style.opacity = '0';
            item.style.transform = 'translateY(-10px)';
            container.appendChild(item);
            
            setTimeout(() => {
                item.style.transition = 'all 0.3s ease';
                item.style.opacity = '1';
                item.style.transform = 'translateY(0)';
            }, 50);
            
            index++;
            setTimeout(showNextItem, delay);
        } else {
            if (onComplete) onComplete();
        }
    }
    
    showNextItem();
}

// 显示聊天消息
function displayMessage(message, role) {
    const chatMessages = document.getElementById('chat-messages');
    if (!chatMessages) return;
    
    const messageEl = document.createElement('div');
    messageEl.className = `message ${role}`;
    
    const messageContent = document.createElement('div');
    messageContent.className = 'message-content';
    
    if (role === 'assistant') {
        // 对模型回复使用Markdown解析
        messageContent.innerHTML = `<div class="markdown-content">${parseMarkdown(message)}</div>`;
    } else {
        messageContent.textContent = message;
    }
    
    const messageTime = document.createElement('div');
    messageTime.className = 'message-time';
    messageTime.textContent = formatTime(Date.now());
    
    messageEl.appendChild(messageContent);
    messageEl.appendChild(messageTime);
    
    chatMessages.appendChild(messageEl);
    
    // 滚动到底部
    scrollToBottom(chatMessages);
}

// 显示流式消息（支持真正的流式追加）- 严格按照提示词标签格式解析
function displayStreamingMessage(messageId, content) {
    console.log('[displayStreamingMessage] ========== 开始解析 ==========');
    console.log(`[displayStreamingMessage] 被调用，messageId:${messageId}, content总长度:${content.length}`);
    console.log(`[displayStreamingMessage] content前300字符:`, content.substring(0, 300));
    
    // 【修复】检测是模型对话页面还是数据库对话页面
    const isDatabaseChat = APP_STATE.currentSession && APP_STATE.currentSession.isDatabase;
    const chatMessagesId = isDatabaseChat ? 'db-chat-messages' : 'chat-messages';
    const chatMessages = document.getElementById(chatMessagesId);
    
    // 【修复】检查会话是否关联了文件（使用currentFile判断，因为预览的文件可能没关联到会话）
    const hasFileId = APP_STATE.currentFile && APP_STATE.currentFile.id;
    
    // 【修复】检查内容中是否包含TITLE_START标签（这是判断是否按标签解析的关键）
    const hasTitleTag = content.includes('<TITLE_START>');
    
    // 【修复问题2】检查消息元素是否已经进入解析模式，如果是，则不再显示原始内容
    let messageEl = document.getElementById(`message-${messageId}`);
    const isAlreadyInParsedMode = messageEl && messageEl.dataset.parsedMode === 'true';
    
    // 调试日志
    console.log(`[displayStreamingMessage] 检查条件:`);
    console.log(`  - isDatabaseChat: ${isDatabaseChat}`);
    console.log(`  - hasFileId: ${hasFileId}`);
    console.log(`  - hasTitleTag: ${hasTitleTag}`);
    console.log(`  - isAlreadyInParsedMode: ${isAlreadyInParsedMode}`);
    console.log(`  - currentSession:`, APP_STATE.currentSession);
    if (APP_STATE.currentSession) {
        console.log(`  - currentSession.fileId:`, APP_STATE.currentSession.fileId);
    }
    
    // 【修复问题2】如果已经进入解析模式，直接跳过原样显示逻辑，继续执行解析逻辑
    if (isAlreadyInParsedMode) {
        console.log('[displayStreamingMessage] 消息已进入解析模式，跳过原样显示逻辑，继续解析');
        // 这里会继续执行后面的标签解析逻辑
    }
    
    // 【修复】根据需求：如果会话未绑定文件，或模型回复未解析到TITLE_START时原样输出
    // 对于数据库对话：如果有TITLE_START标签，按标签解析；否则原样输出
    // 对于模型对话：只有当 hasFileId && hasTitleTag 时，才按标签解析
    const shouldParseByTag = isDatabaseChat ? hasTitleTag : (hasFileId && hasTitleTag);
    
    // 【修复问题2】只有在未进入解析模式且不应该解析标签时，才进入原样显示模式
    if (!shouldParseByTag && !isAlreadyInParsedMode) {
        const reason = isDatabaseChat 
            ? (hasTitleTag ? '' : '模型回复未解析到TITLE_START')
            : (!hasFileId ? '会话未关联文件' : '模型回复未解析到TITLE_START');
        console.log(`[displayStreamingMessage] ${reason}，原样显示回复内容，不进行标签解析`);
        
        // 【修复问题2】在原样显示模式中也检查EMAIL_START标签
        const hasEmailStart = content.includes('<EMAIL_START>');
        if (hasEmailStart) {
            console.log('[displayStreamingMessage] 原样显示模式中检测到邮件发送功能');

            let messageEl = document.getElementById(`message-${messageId}`);

            if (!messageEl) {
                if (!chatMessages) {
                    console.error(`[displayStreamingMessage] 找不到消息容器: ${chatMessagesId}`);
                    return;
                }
                messageEl = document.createElement('div');
                messageEl.id = `message-${messageId}`;
                messageEl.className = 'message assistant';

                const messageContent = document.createElement('div');
                messageContent.className = 'message-content';

                const messageTime = document.createElement('div');
                messageTime.className = 'message-time';
                messageTime.textContent = formatTime(Date.now());

                messageEl.appendChild(messageContent);
                messageEl.appendChild(messageTime);
                chatMessages.appendChild(messageEl);
            }

            // 移除EMAIL标签
            const contentWithoutEmail = content.replace(/<EMAIL_START>[\s\S]*?<EMAIL_END>/g, '').trim();

            // 显示邮件内容，并保留原始模型回复（去掉邮件标签后的内容）
            const messageContent = messageEl.querySelector('.message-content');
            if (messageContent) {
                const htmlBlocks = [];

                if (contentWithoutEmail) {
                    // 如果移除EMAIL标签后还有其他内容，先显示它们
                    const escapedContent = contentWithoutEmail
                        .replace(/&/g, '&amp;')
                        .replace(/</g, '&lt;')
                        .replace(/>/g, '&gt;')
                        .replace(/\\n/g, '<br>');
                    htmlBlocks.push(`<div class="markdown-content">${escapedContent}</div>`);
                }

                // 添加固定的邮件发送成功提示（不显示邮件内容）
                htmlBlocks.push(`
                    <div class="email-success" style="padding: 15px; background: #f0fdf4; border-left: 4px solid #22c55e; border-radius: 8px; margin-top: 10px; color: #166534; font-size: 15px;">
                        邮件发送成功，请注意查收
                    </div>
                `);

                messageContent.innerHTML = htmlBlocks.join('');
            }

            // 滚动到底部
            if (chatMessages) {
                scrollToBottom(chatMessages);
            }
            return; // 直接返回，不执行后续的标签解析
        }
        
        let messageEl = document.getElementById(`message-${messageId}`);
        
        if (!messageEl) {
            if (!chatMessages) {
                console.error(`[displayStreamingMessage] 找不到消息容器: ${chatMessagesId}`);
                return;
            }
            messageEl = document.createElement('div');
            messageEl.id = `message-${messageId}`;
            messageEl.className = 'message assistant';
            
            const messageContent = document.createElement('div');
            messageContent.className = 'message-content';
            
            const messageTime = document.createElement('div');
            messageTime.className = 'message-time';
            messageTime.textContent = formatTime(Date.now());
            
            messageEl.appendChild(messageContent);
            messageEl.appendChild(messageTime);
            chatMessages.appendChild(messageEl);
        }
        
        // 原样显示内容（HTML转义，保留换行）
        const messageContent = messageEl.querySelector('.message-content');
        if (messageContent) {
            const escapedContent = content
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/\n/g, '<br>');
            messageContent.innerHTML = `<div class="markdown-content">${escapedContent}</div>`;
        }
        
        // 滚动到底部
        if (chatMessages) {
            scrollToBottom(chatMessages);
        }
        
        // 【重要】如果有关联文件但还没有TITLE_START标签，需要继续监听，一旦检测到TITLE_START就切换到标签解析模式
        // 这里先返回，等待下次调用时如果检测到TITLE_START标签，就会进入标签解析流程
        return; // 直接返回，不执行后续的标签解析
    }
    
    // 【修复问题1】检查是否是邮件发送功能 - 但只在非标签解析模式下检查
    // 如果已经进入标签解析模式（shouldParseByTag为true），则不应该显示邮件动画
    // 因为标签解析模式已经有自己的显示逻辑，不应该被邮件功能干扰
    // 邮件功能应该只在原样显示模式下使用
    // 注意：EMAIL_START检查已经在原样显示模式分支中处理，这里不需要再检查
    
        // 检查是否已经处于标签解析模式（通过检查消息元素是否已有解析结构来判断）
    // 注意：messageEl 已在上面定义，这里不需要重新获取
    const isAlreadyParsing = messageEl && messageEl.querySelector('.analysis-title-container');
    
    if (isAlreadyParsing) {
        // 如果已经进入标签解析模式，直接跳过EMAIL_START检查，继续执行标签解析逻辑
        console.log('[displayStreamingMessage] 已进入标签解析模式，跳过EMAIL_START检查');
    } else {
        // 只有在还没有进入标签解析模式时，才检查EMAIL_START（作为单独的邮件发送功能）
        const hasEmailStart = content.includes('<EMAIL_START>');
        const hasEmailEnd = content.includes('<EMAIL_END>');
        
        if (hasEmailStart && !hasTitleTag) {
            // 只有在没有TITLE_START标签的情况下，才作为独立的邮件功能处理
            console.log('[displayStreamingMessage] 检测到邮件发送功能（非标签解析模式）');

            if (!messageEl) {
                if (!chatMessages) {
                    console.error(`[displayStreamingMessage] 找不到消息容器: ${chatMessagesId}`);
                    return;
                }
                messageEl = document.createElement('div');
                messageEl.id = `message-${messageId}`;
                messageEl.className = 'message assistant';

                const messageContent = document.createElement('div');
                messageContent.className = 'message-content';

                const messageTime = document.createElement('div');
                messageTime.className = 'message-time';
                messageTime.textContent = formatTime(Date.now());

                messageEl.appendChild(messageContent);
                messageEl.appendChild(messageTime);
                chatMessages.appendChild(messageEl);
            }

            // 移除EMAIL标签
            const contentWithoutEmail = content.replace(/<EMAIL_START>[\s\S]*?<EMAIL_END>/g, '').trim();

            // 显示邮件内容，并保留原始模型回复（去掉邮件标签后的内容）
            const messageContent = messageEl.querySelector('.message-content');
            if (messageContent) {
                const htmlBlocks = [];

                if (contentWithoutEmail) {
                    // 如果移除EMAIL标签后还有其他内容，先显示它们
                    const escapedContent = contentWithoutEmail
                        .replace(/&/g, '&amp;')
                        .replace(/</g, '&lt;')
                        .replace(/>/g, '&gt;')
                        .replace(/\\n/g, '<br>');
                    htmlBlocks.push(`<div class="markdown-content">${escapedContent}</div>`);
                }

                // 添加固定的邮件发送成功提示（不显示邮件内容）
                htmlBlocks.push(`
                    <div class="email-success" style="padding: 15px; background: #f0fdf4; border-left: 4px solid #22c55e; border-radius: 8px; margin-top: 10px; color: #166534; font-size: 15px;">
                        邮件发送成功，请注意查收
                    </div>
                `);

                messageContent.innerHTML = htmlBlocks.join('');
            }

            // 滚动到底部
            if (chatMessages) {
                scrollToBottom(chatMessages);
            }
            return; // 直接返回，不执行后续的分析、SQL、总结等步骤
        }
    }
    
    // 检查是否包含关键标签（hasTitleTag已在上面声明，这里只声明其他标签）
    const hasTasksTag = content.includes('<TASKS_START>');
    const hasAnalysisTag = content.includes('<ANALYSIS_START>');
    const hasSQLTag = content.includes('<SQL_START>');
    const hasChartDataTag = content.includes('<CHART_DATA>');
    
    // 【修复】在进入标签解析模式之前，先过滤掉EMAIL标签（EMAIL是独立功能，不属于标签解析体系）
    // 如果内容中包含EMAIL_START标签，需要移除标签并显示固定提示
    const hasEmailTagInContent = content.includes('<EMAIL_START>');
    if (hasEmailTagInContent) {
        console.log('[displayStreamingMessage] 检测到EMAIL标签，移除标签并显示固定提示');

        // 从content中移除所有EMAIL标签
        content = content.replace(/<EMAIL_START>[\s\S]*?<EMAIL_END>/g, '').trim();
        console.log('[displayStreamingMessage] 移除EMAIL标签后的content:', content);

        // 获取或创建messageEl
        if (!messageEl) {
            messageEl = document.getElementById(`message-${messageId}`);
        }

        if (messageEl) {
            const messageContent = messageEl.querySelector('.message-content');
            if (messageContent) {
                // 构建HTML：原始内容（如果有）+ 固定邮件提示
                const htmlBlocks = [];

                if (content) {
                    // 如果移除EMAIL标签后还有其他内容，先显示它们
                    const escapedContent = content
                        .replace(/&/g, '&amp;')
                        .replace(/</g, '&lt;')
                        .replace(/>/g, '&gt;')
                        .replace(/\\n/g, '<br>');
                    htmlBlocks.push(`<div class="markdown-content">${escapedContent}</div>`);
                }

                // 添加固定的邮件发送成功提示
                htmlBlocks.push(`
                    <div class="email-success" style="padding: 15px; background: #f0fdf4; border-left: 4px solid #22c55e; border-radius: 8px; margin-top: 10px; color: #166534; font-size: 15px;">
                        邮件发送成功，请注意查收
                    </div>
                `);

                messageContent.innerHTML = htmlBlocks.join('');
            }

            // 滚动到底部
            if (chatMessages) {
                scrollToBottom(chatMessages);
            }
        }

        // 【重要】直接返回，不进入标签解析模式
        console.log('[displayStreamingMessage] EMAIL标签已处理，直接返回');
        return;
    }

    console.log(`[displayStreamingMessage] ✓✓✓ 进入标签解析模式`);
    console.log(`[displayStreamingMessage] 标签检测结果:`);
    console.log(`  - TITLE_START: ${hasTitleTag}`);
    console.log(`  - TASKS_START: ${hasTasksTag}`);
    console.log(`  - ANALYSIS_START: ${hasAnalysisTag}`);
    console.log(`  - SQL_START: ${hasSQLTag}`);
    console.log(`  - CHART_DATA: ${hasChartDataTag}`);
    
    // messageEl 已在上面定义（在检查isAlreadyInParsedMode时），这里只需确保存在
    if (!messageEl) {
        messageEl = document.getElementById(`message-${messageId}`);
    }
    
    // 【修复】检查消息元素是否已经存在，如果存在但结构是原样显示的，需要重新初始化为标签解析模式
    const needsReinit = messageEl && !messageEl.querySelector('.analysis-title-container');
    
    if (!messageEl || needsReinit) {
        if (needsReinit) {
            console.log('[displayStreamingMessage] 检测到标签，从原样显示模式切换到标签解析模式');
            // 清空原样显示的内容，重新初始化为标签解析结构
            // 注意：保留 message-time 元素
            const messageContent = messageEl.querySelector('.message-content');
            if (messageContent) {
                messageContent.innerHTML = `
                    <div class="analysis-title-container"></div>
                    <div class="analysis-tasks-container"></div>
                    <div class="analysis-findings-container"></div>
                    <div class="analysis-summary-container"></div>
                `;
            }
            // 确保 message-time 元素存在
            if (!messageEl.querySelector('.message-time')) {
                const messageTime = document.createElement('div');
                messageTime.className = 'message-time';
                messageTime.textContent = formatTime(Date.now());
                messageEl.appendChild(messageTime);
            }
        } else {
            console.log('[displayStreamingMessage] 创建新消息元素（标签解析模式）');
            if (!chatMessages) {
                console.error(`[displayStreamingMessage] 找不到消息容器: ${chatMessagesId}`);
                return;
            }
            messageEl = document.createElement('div');
            messageEl.id = `message-${messageId}`;
            messageEl.className = 'message assistant';
            
            const messageContent = document.createElement('div');
            messageContent.className = 'message-content';
            messageContent.innerHTML = `
                <div class="analysis-title-container"></div>
                <div class="analysis-tasks-container"></div>
                <div class="analysis-findings-container"></div>
                <div class="analysis-summary-container"></div>
            `;
            
            const messageTime = document.createElement('div');
            messageTime.className = 'message-time';
            messageTime.textContent = formatTime(Date.now());
            
            messageEl.appendChild(messageContent);
            messageEl.appendChild(messageTime);
            chatMessages.appendChild(messageEl);
        }
        
        // 初始化追踪状态（记录已显示的任务和分析项）
        messageEl.dataset.lastDisplayedLength = '0';
        messageEl.dataset.displayedTaskIds = '[]';  // 已显示的任务ID列表
        messageEl.dataset.displayedAnalysisIds = '[]';  // 已显示的分析ID列表
        messageEl.dataset.titleDisplayed = 'false';
        messageEl.dataset.sqlDisplayed = 'false';
        messageEl.dataset.parsedMode = 'true';  // 【修复问题2】标记为已进入解析模式，防止后续显示原始内容
    } else {
        console.log('[displayStreamingMessage] 更新已存在的消息元素（标签解析模式）');
        // 【修复问题2】确保已存在的消息元素也标记为解析模式
        if (!messageEl.dataset.parsedMode) {
            messageEl.dataset.parsedMode = 'true';
        }
    }
    
    // ============================================================================
    // 【重要】根据提示词格式，严格解析标签式响应
    // 第一阶段：解析四个标签（TITLE, TASKS, ANALYSIS, SQL）
    // 第二阶段：解析 <CHART_DATA> 中的JSON
    // ============================================================================
    
    // 使用 parseStreamingJSON 函数解析标签式内容
    console.log('[displayStreamingMessage] 调用parseStreamingJSON，内容长度:', content.length);
    const parsed = parseStreamingJSON(content);
    const firstPhase = parsed.firstPhase;
    const summaryJson = parsed.summaryPhase;
    console.log('[displayStreamingMessage] parseStreamingJSON返回: firstPhase=', !!firstPhase, ', summaryJson=', !!summaryJson);
    
    // 获取所有容器元素
    const titleContainer = messageEl.querySelector('.analysis-title-container');
    const tasksContainer = messageEl.querySelector('.analysis-tasks-container');
    const findingsContainer = messageEl.querySelector('.analysis-findings-container');
    const summaryContainer = messageEl.querySelector('.analysis-summary-container');
    
    // ========== 第一阶段：显示分析计划（来自标签解析，增量更新） ==========
    if (firstPhase) {
        // 获取已显示的状态
        let displayedTaskIds = JSON.parse(messageEl.dataset.displayedTaskIds || '[]');
        let displayedAnalysisIds = JSON.parse(messageEl.dataset.displayedAnalysisIds || '[]');
        const titleDisplayed = messageEl.dataset.titleDisplayed === 'true';
        const sqlDisplayed = messageEl.dataset.sqlDisplayed === 'true';
        
        // 1. 显示标题（只显示一次，支持实时更新内容）
        if (firstPhase.title && titleContainer) {
            let titleTextEl = titleContainer.querySelector('.analysis-title-text');
            if (!titleTextEl) {
                titleContainer.innerHTML = `<div class="analysis-title"><h3>📊 <span class="analysis-title-text"></span></h3></div>`;
                titleTextEl = titleContainer.querySelector('.analysis-title-text');
                console.log('[标签显示] 标题容器已创建');
            }
            
            const decodedTitle = decodeUnicodeString(firstPhase.title);
            const escapedTitle = decodedTitle
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;');
            
            // 实时更新标题内容（即使未完整也显示）
            if (titleTextEl.innerHTML !== escapedTitle) {
                titleTextEl.innerHTML = escapedTitle;
                console.log('[标签显示] 标题已更新:', decodedTitle);
                messageEl.dataset.titleDisplayed = 'true';
            }
        }
        
        // 2. 显示任务列表（增量添加新任务）
        if (firstPhase.tasks && Array.isArray(firstPhase.tasks) && tasksContainer) {
            let taskListEl = tasksContainer.querySelector('.task-list');
            if (!taskListEl) {
                tasksContainer.innerHTML = `
                    <div class="task-list-container">
                        <h4>📋 分析任务：</h4>
                        <ul class="task-list"></ul>
                    </div>
                `;
                taskListEl = tasksContainer.querySelector('.task-list');
                console.log('[标签显示] 任务列表容器已创建');
            }
            
            // 【关键修复】支持新增任务和更新已有任务的描述（流式更新）
            firstPhase.tasks.forEach(task => {
                const taskId = task.id;
                const taskDesc = decodeUnicodeString(task.description);
                const escapedTaskDesc = taskDesc
                    .replace(/&/g, '&amp;')
                    .replace(/</g, '&lt;')
                    .replace(/>/g, '&gt;');
                
                // 查找是否已存在该任务
                const existingTaskItem = taskListEl.querySelector(`[data-task-id="${taskId}"]`);
                
                if (!existingTaskItem) {
                    // 新任务：创建新的任务项
                    if (!displayedTaskIds.includes(taskId)) {
                        const taskHtml = `
                            <li class="task-item expandable" data-task-id="${taskId}">
                                <div class="task-header">
                                    <span class="task-icon">⏳</span>
                                    <span class="task-number">${taskId}.</span>
                                    <span class="task-description">${escapedTaskDesc}</span>
                                    <span class="expand-icon">▼</span>
                                </div>
                                <div class="task-detail" style="display: none;"></div>
                            </li>
                        `;
                        
                        taskListEl.insertAdjacentHTML('beforeend', taskHtml);
                        displayedTaskIds.push(taskId);
                        console.log(`[标签显示] 任务${taskId}已创建（实时）:`, taskDesc);
                        
                        // 绑定点击事件
                        const newTaskItem = taskListEl.querySelector(`[data-task-id="${taskId}"]`);
                        const taskHeader = newTaskItem?.querySelector('.task-header');
                        if (taskHeader && newTaskItem) {
                            taskHeader.style.cursor = 'pointer';
                            taskHeader.addEventListener('click', (e) => {
                                e.stopPropagation();
                                const detailDiv = newTaskItem.querySelector('.task-detail');
                                const expandIcon = newTaskItem.querySelector('.expand-icon');
                                if (detailDiv && expandIcon) {
                                    const isExpanded = detailDiv.style.display !== 'none';
                                    detailDiv.style.display = isExpanded ? 'none' : 'block';
                                    expandIcon.textContent = isExpanded ? '▼' : '▲';
                                    newTaskItem.classList.toggle('expanded', !isExpanded);
                                }
                            });
                        }
                    }
                } else {
                    // 已存在的任务：更新任务描述（支持流式更新完整内容）
                    const taskDescEl = existingTaskItem.querySelector('.task-description');
                    if (taskDescEl) {
                        const currentDesc = taskDescEl.textContent || taskDescEl.innerText;
                        // 只有当新描述更长或不同时才更新（避免重复更新相同内容）
                        if (taskDesc.length > currentDesc.length || taskDesc !== currentDesc) {
                            taskDescEl.innerHTML = escapedTaskDesc;
                            console.log(`[标签显示] 任务${taskId}描述已更新（流式）:`, taskDesc);
                            console.log(`[标签显示] 旧描述长度: ${currentDesc.length}, 新描述长度: ${taskDesc.length}`);
                        }
                    }
                }
            });
            
            // 更新已显示的任务列表
            messageEl.dataset.displayedTaskIds = JSON.stringify(displayedTaskIds);
        }
        
        // 3. 显示分析详情（逐条增量添加，支持实时更新）
        if (firstPhase.analysis && Array.isArray(firstPhase.analysis) && tasksContainer) {
            firstPhase.analysis.forEach(item => {
                const taskId = item.taskId;
                const analysisContent = decodeUnicodeString(item.content);
                const analysisKey = `${taskId}`;
                
                // 查找对应的任务项
                const taskItem = tasksContainer.querySelector(`[data-task-id="${taskId}"]`);
                if (taskItem) {
                    let detailDiv = taskItem.querySelector('.task-detail');
                    if (detailDiv) {
                        const escapedContent = analysisContent
                            .replace(/&/g, '&amp;')
                            .replace(/</g, '&lt;')
                            .replace(/>/g, '&gt;')
                            .replace(/\n/g, '<br>');
                        
                        // 实时更新分析内容（即使未完整也显示）
                        if (detailDiv.innerHTML !== escapedContent) {
                            detailDiv.innerHTML = escapedContent;
                            detailDiv.style.display = 'block'; // 自动展开
                            
                            const expandIcon = taskItem.querySelector('.expand-icon');
                            if (expandIcon) expandIcon.textContent = '▲';
                            taskItem.classList.add('expanded');
                            
                            // 更新任务状态为processing
                            const taskIcon = taskItem.querySelector('.task-icon');
                            if (taskIcon && taskIcon.textContent !== '🔄') {
                                taskIcon.textContent = '🔄';
                            }
                            
                            if (!displayedAnalysisIds.includes(analysisKey)) {
                                console.log(`[标签显示] 任务${taskId}分析详情已创建（实时）`);
                                displayedAnalysisIds.push(analysisKey);
                            } else {
                                console.log(`[标签显示] 任务${taskId}分析详情已更新（实时）`);
                            }
                        }
                    }
                }
            });
            
            // 更新已显示的分析列表
            messageEl.dataset.displayedAnalysisIds = JSON.stringify(displayedAnalysisIds);
        }
        
        // 4. 显示SQL执行提示（只显示一次）
        if (firstPhase.sql && summaryContainer && !sqlDisplayed) {
            if (!summaryContainer.querySelector('.processing-indicator')) {
                summaryContainer.innerHTML = `
                    <div class="processing-indicator">
                        <div class="processing-icon">⏳</div>
                        <div class="processing-text">SQL执行中，结果处理中</div>
                        <div class="processing-dots">
                            <span class="dot">.</span>
                            <span class="dot">.</span>
                            <span class="dot">.</span>
                        </div>
                    </div>
                `;
                messageEl.dataset.sqlDisplayed = 'true';
                console.log('[标签显示] SQL执行提示已显示');
            }
        }
    }
    
    // ========== 第二阶段：显示总结和关键发现（来自summaryJson） ==========
    if (summaryJson) {
        console.log('[displayStreamingMessage] ✓✓✓ 开始显示第二阶段（总结数据）');
        console.log('[displayStreamingMessage] summaryJson:', summaryJson);
        
        // 1. 更新任务状态
        if (summaryJson.taskStatus && Array.isArray(summaryJson.taskStatus) && tasksContainer) {
            console.log('[displayStreamingMessage] 更新任务状态，任务数:', summaryJson.taskStatus.length);
            summaryJson.taskStatus.forEach(task => {
                const taskId = task.taskId;
                const status = task.status; // "completed" or "failed"
                
                const taskItem = tasksContainer.querySelector(`[data-task-id="${taskId}"]`);
                if (taskItem) {
                    const taskIcon = taskItem.querySelector('.task-icon');
                    if (taskIcon) {
                        taskIcon.textContent = status === 'completed' ? '✅' : '❌';
                    }
                    
                    // 折叠任务详情
                    const detailDiv = taskItem.querySelector('.task-detail');
                    const expandIcon = taskItem.querySelector('.expand-icon');
                    if (detailDiv && expandIcon) {
                        detailDiv.style.display = 'none';
                        expandIcon.textContent = '▼';
                        taskItem.classList.remove('expanded');
                    }
                    
                    console.log(`[第二阶段] 任务${taskId}状态更新为:`, status);
                }
            });
        }
        
        // 2. 显示关键发现（支持流式更新：逐条添加，每条内容可实时更新）
        if (summaryJson.keyFindings && Array.isArray(summaryJson.keyFindings) && findingsContainer) {
            if (!findingsContainer.querySelector('.findings-container')) {
                findingsContainer.innerHTML = `
                    <div class="findings-container">
                        <h4>💡 关键发现：</h4>
                        <ul class="findings-list"></ul>
                    </div>
                `;
            }
            
            const findingsList = findingsContainer.querySelector('.findings-list');
            const currentItems = findingsList.querySelectorAll('.finding-item');
            const currentFindingsCount = currentItems.length;
            
            // 处理所有发现项（新增或更新）
            summaryJson.keyFindings.forEach((finding, index) => {
                const decodedFinding = decodeUnicodeString(finding);
                const escapedFinding = decodedFinding
                    .replace(/&/g, '&amp;')
                    .replace(/</g, '&lt;')
                    .replace(/>/g, '&gt;');
                
                if (index < currentFindingsCount) {
                    // 更新已存在的项（流式更新内容）
                    const existingItem = currentItems[index];
                    const textSpan = existingItem.querySelector('.finding-text');
                    if (textSpan && textSpan.innerHTML !== escapedFinding) {
                        textSpan.innerHTML = escapedFinding;
                        console.log(`[第二阶段] 关键发现${index + 1}已更新（流式），长度:`, decodedFinding.length);
                    }
                } else {
                    // 添加新的发现项
                    const li = document.createElement('li');
                    li.className = 'finding-item';
                    li.innerHTML = `<span class="finding-icon">💡</span><span class="finding-text">${escapedFinding}</span>`;
                    li.style.opacity = '0';
                    li.style.transform = 'translateY(-10px)';
                    findingsList.appendChild(li);
                    
                    // 淡入动画
                    setTimeout(() => {
                        li.style.transition = 'all 0.3s ease';
                        li.style.opacity = '1';
                        li.style.transform = 'translateY(0)';
                    }, 50);
                    
                    console.log(`[第二阶段] 关键发现${index + 1}已创建（流式）`);
                }
            });
            
            console.log(`[第二阶段] 关键发现总数: ${summaryJson.keyFindings.length}`);
        }
        
        // 3. 显示总结（支持流式更新）
        if (summaryJson.summary && summaryContainer) {
            // 移除处理中提示（只执行一次）
            const processingEl = summaryContainer.querySelector('.processing-indicator');
            if (processingEl) processingEl.remove();
            
            // 【修复】支持流式更新：如果summary容器不存在则创建，存在则只更新内容
            let summaryContainerDiv = summaryContainer.querySelector('.summary-container');
            if (!summaryContainerDiv) {
                summaryContainer.innerHTML = `
                    <div class="summary-container">
                        <h4>📝 分析总结：</h4>
                        <p class="summary-text"></p>
                    </div>
                `;
                summaryContainerDiv = summaryContainer.querySelector('.summary-container');
                console.log('[第二阶段] 分析总结容器已创建');
            }
            
            // 实时更新总结文本内容（流式）
            const summaryTextEl = summaryContainerDiv.querySelector('.summary-text');
            if (summaryTextEl) {
                const decodedSummary = decodeUnicodeString(summaryJson.summary);
                const escapedSummary = decodedSummary
                    .replace(/&/g, '&amp;')
                    .replace(/</g, '&lt;')
                    .replace(/>/g, '&gt;')
                    .replace(/\n/g, '<br>');
                
                // 只在内容改变时更新（避免重复渲染）
                if (summaryTextEl.innerHTML !== escapedSummary) {
                    summaryTextEl.innerHTML = escapedSummary;
                    console.log('[第二阶段] 分析总结已更新（流式），长度:', decodedSummary.length);
                }
            }
        }
    }
    
    // ========== 废弃：以下为旧的标签解析逻辑（已完全禁用） ==========
    // 处理任务列表（后端流式发送，前端实时追加）
    // 注：此逻辑已废弃，现在使用纯JSON解析（见上方代码）
    /*
    if (tasksContainer) {
        // 提取所有的 <TASKS_START>...</TASKS_END> 标签
        const tasksMatches = content.match(/<TASKS_START>([\s\S]*?)<TASKS_END>/g);
        console.log('[任务列表诊断] tasksMatches数量:', tasksMatches ? tasksMatches.length : 0);
        if (tasksMatches) {
            // 获取或创建任务列表容器
            let taskListEl = tasksContainer.querySelector('.task-list');
            if (!taskListEl) {
                console.log('[任务列表诊断] 创建新的任务列表容器');
                tasksContainer.innerHTML = `
                    <div class="task-list-container">
                        <h4>📋 分析任务：</h4>
                        <ul class="task-list"></ul>
                    </div>
                `;
                taskListEl = tasksContainer.querySelector('.task-list');
                messageEl.dataset.tasksAccumulated = '';
            }
            
            // 合并所有任务内容
            let fullTasks = '';
            tasksMatches.forEach(match => {
                const tasksPart = match.replace(/<TASKS_START>|<TASKS_END>/g, '');
                fullTasks += tasksPart;
            });
            console.log('[任务列表诊断] fullTasks长度:', fullTasks.length);
            console.log('[任务列表诊断] fullTasks前200字符:', fullTasks.substring(0, 200));
            
            // Unicode解码
            console.log('[任务列表诊断] 开始Unicode解码...');
            let decodedTasks;
            try {
                decodedTasks = decodeUnicodeString(fullTasks);
                console.log('[任务列表诊断] Unicode解码成功');
                console.log('[任务列表诊断] decodedTasks长度:', decodedTasks.length);
                console.log('[任务列表诊断] decodedTasks前200字符:', decodedTasks.substring(0, 200));
            } catch (error) {
                console.error('[任务列表诊断] Unicode解码异常:', error);
                decodedTasks = fullTasks; // 使用原始字符串
            }
            
            // 解析任务列表
            console.log('[任务列表诊断] 开始解析任务列表...');
            const allLines = decodedTasks.split('\n');
            console.log('[任务列表诊断] 分割后的行数:', allLines.length);
            
            // 增强的任务行匹配：支持多种格式
            // 1. 标准格式: "1. 任务描述"
            // 2. 带空格格式: "1 . 任务描述" 或 "1 .任务描述"
            // 3. 中文句号: "1。任务描述"
            // 4. 带括号: "1) 任务描述" 或 "(1) 任务描述"
            const taskLines = allLines
                .map(line => line.trim())
                .filter(line => {
                    // 匹配多种任务编号格式
                    const matched = line.match(/^\d+[\s]*[\.。\)）]/) || line.match(/^[\(（]\d+[\)）]/);
                    if (matched) {
                        console.log('[任务匹配] 成功匹配行:', line);
                    }
                    return matched;
                });
            
            console.log('[任务列表诊断] 过滤后的任务行数:', taskLines.length);
            console.log('[任务列表诊断] 任务行内容:', taskLines);
            
            const lastTaskCount = parseInt(messageEl.dataset.tasksDisplayedCount || '0');
            
            // 检查DOM中实际的任务项数量
            const actualTaskItems = taskListEl.querySelectorAll('.task-item');
            const actualTaskCount = actualTaskItems.length;
            
            console.log('[任务列表诊断] lastTaskCount(dataset):', lastTaskCount);
            console.log('[任务列表诊断] actualTaskCount(DOM):', actualTaskCount);
            console.log('[任务列表诊断] taskLines.length:', taskLines.length);
            
            // 如果DOM中的任务数量少于应该显示的数量，需要创建缺失的任务项
            const needsCreation = taskLines.length > actualTaskCount;
            console.log('[任务列表诊断] 是否需要创建任务:', needsCreation);
            
            if (needsCreation) {
                console.log(`[流式任务] 任务从${actualTaskCount}条增加到${taskLines.length}条（修复dataset=${lastTaskCount}）`);
                    
                    // 使用实际的DOM任务数量作为起点
                    for (let i = actualTaskCount; i < taskLines.length; i++) {
                        const line = taskLines[i];
                        // 增强的任务ID和描述提取，支持多种格式
                        // 格式1: "1. 任务描述"
                        // 格式2: "1 . 任务描述" 或 "1 .任务描述"
                        // 格式3: "1。任务描述"
                        // 格式4: "1) 任务描述"
                        // 格式5: "(1) 任务描述"
                        let match = line.match(/^(\d+)[\s]*[\.。]\s*(.+)$/);
                        if (!match) match = line.match(/^(\d+)[\s]*[\)）]\s*(.+)$/);
                        if (!match) match = line.match(/^[\(（](\d+)[\)）]\s*(.+)$/);
                        
                        if (match) {
                            const taskId = parseInt(match[1]);
                            const taskDesc = match[2].trim();
                            
                            console.log(`[任务创建] 任务${taskId} 原始描述:`, taskDesc);
                            console.log(`[任务创建] 任务${taskId} 描述长度:`, taskDesc.length);
                            
                            // HTML转义
                            const escapedTaskDesc = taskDesc
                                .replace(/&/g, '&amp;')
                                .replace(/</g, '&lt;')
                                .replace(/>/g, '&gt;');
                            
                            const taskHtml = `
                                <li class="task-item expandable" data-task-id="${taskId}">
                                    <div class="task-header">
                                        <span class="task-icon">⏳</span>
                                        <span class="task-number">${taskId}.</span>
                                        <span class="task-description">${escapedTaskDesc}</span>
                                        <span class="expand-icon">▼</span>
                                    </div>
                                    <div class="task-detail" style="display: none;"></div>
                                </li>
                            `;
                            
                            taskListEl.insertAdjacentHTML('beforeend', taskHtml);
                            console.log(`[流式任务] 第${i + 1}条任务已创建，描述长度:${taskDesc.length}`);
                            
                            // 为新创建的任务项绑定点击事件
                            const newTaskItem = taskListEl.querySelector(`[data-task-id="${taskId}"]`);
                            const taskHeader = newTaskItem?.querySelector('.task-header');
                            if (taskHeader && newTaskItem) {
                                taskHeader.style.cursor = 'pointer';
                                taskHeader.addEventListener('click', (e) => {
                                    e.stopPropagation();
                                    const detailDiv = newTaskItem.querySelector('.task-detail');
                                    const expandIcon = newTaskItem.querySelector('.expand-icon');
                                    if (detailDiv && expandIcon) {
                                        const isExpanded = detailDiv.style.display !== 'none';
                                        detailDiv.style.display = isExpanded ? 'none' : 'block';
                                        expandIcon.textContent = isExpanded ? '▼' : '▲';
                                        newTaskItem.classList.toggle('expanded', !isExpanded);
                                        console.log(`[任务点击] 任务${taskId} ${isExpanded ? '折叠' : '展开'}`);
                                    }
                                });
                            }
                        }
                    }
                    
                    messageEl.dataset.tasksDisplayedCount = taskLines.length.toString();
                } else {
                    console.log('[任务列表诊断] 跳过创建，但会更新已有任务描述');
                    
                    // 即使不需要创建新任务，也要更新已有任务的描述（因为早期chunk可能是不完整的）
                    for (let i = 0; i < taskLines.length; i++) {
                        const line = taskLines[i];
                        // 使用增强的任务ID和描述提取
                        let match = line.match(/^(\d+)[\s]*[\.。]\s*(.+)$/);
                        if (!match) match = line.match(/^(\d+)[\s]*[\)）]\s*(.+)$/);
                        if (!match) match = line.match(/^[\(（](\d+)[\)）]\s*(.+)$/);
                        
                        if (match) {
                            const taskId = parseInt(match[1]);
                            const taskDesc = match[2].trim();
                            
                            // 查找对应的任务项
                            const taskItem = taskListEl.querySelector(`[data-task-id="${taskId}"]`);
                            if (taskItem) {
                                const taskDescEl = taskItem.querySelector('.task-description');
                                if (taskDescEl) {
                                    const currentDesc = taskDescEl.textContent;
                                    
                                    // 如果新描述更长，说明是更完整的版本，需要更新
                                    if (taskDesc.length > currentDesc.length) {
                                        // HTML转义
                                        const escapedTaskDesc = taskDesc
                                            .replace(/&/g, '&amp;')
                                            .replace(/</g, '&lt;')
                                            .replace(/>/g, '&gt;');
                                        taskDescEl.textContent = taskDesc;
                                        console.log(`[任务更新] 任务${taskId} 描述从${currentDesc.length}字符更新到${taskDesc.length}字符`);
                                        console.log(`[任务更新] 新描述:`, taskDesc);
                                    }
                                }
                            }
                        }
                    }
                }
        } else {
            console.log('[任务列表诊断] 没有找到TASKS标签');
        }
    } else {
        console.log('[任务列表诊断] tasksContainer不存在');
    }
    */
    
    // 【废弃】流式显示分析过程文本 - 正确解析 <ANALYSIS_START><ANALYSIS_END> 标签
    if (false && tasksContainer) { // 已禁用
        // 提取 <ANALYSIS_START> 和 <ANALYSIS_END> 之间的内容
        const analysisMatches = content.match(/<ANALYSIS_START>([\s\S]*?)(?:<ANALYSIS_END>|$)/);
        
        if (analysisMatches) {
            const analysisText = analysisMatches[1];
            console.log('[ANALYSIS解析] 提取到分析文本，长度:', analysisText.length);
            
            const lastDisplayedLength = parseInt(messageEl.dataset.lastDisplayedLength || '0');
            console.log(`[ANALYSIS流式检查] 当前:${analysisText.length}字节, 上次:${lastDisplayedLength}字节`);
            
            // 只有当有新内容时才更新
            if (analysisText.length > lastDisplayedLength) {
                console.log(`[ANALYSIS流式更新] 从${lastDisplayedLength}字节更新到${analysisText.length}字节`);
                
                // Unicode解码
                const decodedAnalysis = decodeUnicodeString(analysisText);
                console.log('[ANALYSIS解析] Unicode解码后长度:', decodedAnalysis.length);
                
                // 解析任务详情：提取"任务X：内容" 格式
                // 根据提示词示例，格式为：
                // 任务1：描述内容
                // 
                // 任务2：描述内容
                const taskDetailsMap = {};
                
                // 先按"任务X："分割，支持中英文冒号
                // 使用split + 正向前瞻来保留分隔符
                const parts = decodedAnalysis.split(/(?=(?:任务|第)\d+(?:步)?[：:])/);
                console.log(`[ANALYSIS解析] 按任务标记分割，共${parts.length}部分`);
                
                parts.forEach((part, index) => {
                    const trimmedPart = part.trim();
                    if (trimmedPart.length === 0) return;
                    
                    // 提取任务编号和内容
                    // 匹配：任务1：内容 或 第1步：内容
                    const taskMatch = trimmedPart.match(/^(?:任务|第)(\d+)(?:步)?[：:]\s*([\s\S]*)$/);
                    if (taskMatch) {
                        const taskId = parseInt(taskMatch[1]);
                        const taskDetail = taskMatch[2].trim();
                        
                        if (taskDetail.length > 0) {
                            taskDetailsMap[taskId] = taskDetail;
                            console.log(`[ANALYSIS任务解析] 任务${taskId}详情长度: ${taskDetail.length}字符`);
                            console.log(`[ANALYSIS任务解析] 任务${taskId}详情前100字符:`, taskDetail.substring(0, 100));
                        } else {
                            console.log(`[ANALYSIS任务解析] 任务${taskId}详情为空（可能流式传输中）`);
                        }
                    } else {
                        // 如果第一部分不匹配（可能是前置说明文字），跳过
                        if (index === 0 && trimmedPart.length < 200) {
                            console.log(`[ANALYSIS解析] 跳过前置内容（长度${trimmedPart.length}）:`, trimmedPart.substring(0, 50));
                        } else {
                            console.log(`[ANALYSIS解析] 无法解析的部分${index}:`, trimmedPart.substring(0, 100));
                        }
                    }
                });
                
                // 更新每个任务的详情并展开
                if (Object.keys(taskDetailsMap).length > 0) {
                    console.log(`[ANALYSIS更新] 准备更新${Object.keys(taskDetailsMap).length}个任务的详情`);
                    Object.keys(taskDetailsMap).forEach(taskIdStr => {
                        const taskId = parseInt(taskIdStr);
                        const taskDetail = taskDetailsMap[taskId];
                        updateTaskDetailRealtime(tasksContainer, taskId, taskDetail, true); // true表示展开
                    });
                    
                    // 动态更新任务状态为processing
                    const lastTaskMention = parseInt(messageEl.dataset.lastTaskMention || '0');
                    const maxTaskId = Math.max(...Object.keys(taskDetailsMap).map(id => parseInt(id)));
                    
                    if (maxTaskId > lastTaskMention) {
                        console.log(`[ANALYSIS任务更新] 从任务${lastTaskMention}更新到任务${maxTaskId}`);
                        for (let i = lastTaskMention + 1; i <= maxTaskId; i++) {
                            updateTaskStatus(messageEl, i, 'processing');
                        }
                        messageEl.dataset.lastTaskMention = maxTaskId.toString();
                    }
                } else {
                    console.log('[ANALYSIS解析] 未能提取到任何任务详情');
                }
                
                messageEl.dataset.lastDisplayedLength = analysisText.length.toString();
            } else {
                console.log('[ANALYSIS流式检查] 内容未增加，跳过更新');
            }
        } else {
            console.log('[ANALYSIS解析] 暂未检测到<ANALYSIS_START>标签（可能还在等待）');
        }
    }
    
    // 【废弃】检测SQL生成阶段 - 显示"结果处理中..."动画
    // 注意：新的JSON格式不需要检测SQL_START标签
    if (false && summaryContainer) { // 已禁用
        const hasSqlStart = content.includes('<SQL_START>');
        const hasChartData = content.includes('<CHART_DATA>');
        
        // 如果检测到SQL_START但还没有CHART_DATA，显示处理动画
        if (hasSqlStart && !hasChartData && !summaryContainer.querySelector('.processing-indicator')) {
            console.log('[SQL生成] 检测到SQL_START，显示结果处理中...');
            summaryContainer.innerHTML = `
                <div class="processing-indicator">
                    <div class="processing-icon">⏳</div>
                    <div class="processing-text">结果处理中</div>
                    <div class="processing-dots">
                        <span class="dot">.</span>
                        <span class="dot">.</span>
                        <span class="dot">.</span>
                    </div>
                </div>
            `;
        }
        
        // 如果检测到CHART_DATA，移除处理动画（后面会显示实际内容）
        if (hasChartData && summaryContainer.querySelector('.processing-indicator')) {
            console.log('[SQL生成] 检测到CHART_DATA，移除处理动画');
            const processingEl = summaryContainer.querySelector('.processing-indicator');
            if (processingEl) {
                processingEl.remove();
            }
        }
    }
    
    // 【废弃】实时解析并显示关键发现（流式 - 逐字符）
    // 注意：新逻辑已在上方的summaryJson处理中完成
    if (false && findingsContainer) { // 已禁用
        // 尝试从CHART_DATA中提取关键发现
        const partialChartMatch = content.match(/<CHART_DATA>([\s\S]*?)(?:<\/CHART_DATA>|$)/);
        if (partialChartMatch) {
            const partialJson = partialChartMatch[1];
            console.log('[流式检测] 检测到部分CHART_DATA，长度:', partialJson.length);
            
            // 尝试提取keyFindings（即使JSON不完整）
            const findingsMatch = partialJson.match(/"keyFindings"\s*:\s*\[([\s\S]*?)(?:\]|$)/);
            if (findingsMatch) {
                console.log('[流式发现] 匹配到keyFindings，内容长度:', findingsMatch[1].length);
                
                const findingsStr = '[' + findingsMatch[1] + (findingsMatch[1].endsWith(']') ? '' : ']');
                try {
                    // 尝试解析为JSON数组
                    let findings = [];
                    const findingItems = findingsMatch[1].match(/"([^"]+)"/g);
                    
                    if (findingItems) {
                        findings = findingItems.map(item => {
                            // 移除首尾引号并进行unicode解码
                            const rawString = item.replace(/^"|"$/g, '');
                            return decodeUnicodeString(rawString);
                        });
                    }
                    
                    // 初始化容器（如果还没有）
                    if (!findingsContainer.querySelector('.findings-container')) {
                        findingsContainer.innerHTML = `
                            <div class="findings-container">
                                <h4>💡 关键发现：</h4>
                                <ul class="findings-list"></ul>
                            </div>
                        `;
                    }
                    
                    const findingsList = findingsContainer.querySelector('.findings-list');
                    const lastFindingsCount = parseInt(messageEl.dataset.lastFindingsCount || '0');
                    
                    // 检查是否有新的发现项
                    if (findings.length > lastFindingsCount) {
                        console.log(`[流式发现] 发现从${lastFindingsCount}条增加到${findings.length}条`);
                        
                        // 只添加新的发现项
                        for (let i = lastFindingsCount; i < findings.length; i++) {
                            const li = document.createElement('li');
                            li.className = 'finding-item';
                            li.dataset.findingIndex = i.toString();
                            li.innerHTML = `<span class="finding-icon">💡</span><span class="finding-text"></span>`;
                            li.style.opacity = '0';
                            li.style.transform = 'translateY(-10px)';
                            findingsList.appendChild(li);
                            
                            // 淡入动画
                            setTimeout(() => {
                                li.style.transition = 'all 0.3s ease';
                                li.style.opacity = '1';
                                li.style.transform = 'translateY(0)';
                            }, 50);
                            
                            console.log(`[流式发现] 第${i + 1}条发现项已创建`);
                        }
                        
                        messageEl.dataset.lastFindingsCount = findings.length.toString();
                    }
                    
                    // 流式更新每个发现的文本内容（逐字符）
                    for (let i = 0; i < findings.length; i++) {
                        const findingItem = findingsList.querySelector(`[data-finding-index="${i}"]`);
                        if (findingItem) {
                            const findingTextEl = findingItem.querySelector('.finding-text');
                            const fullText = findings[i];
                            const currentText = findingTextEl.textContent || '';
                            
                            if (fullText.length > currentText.length) {
                                // 每次最多追加15个字符，实现流式效果
                                const maxCharsPerUpdate = 15;
                                const remainingChars = fullText.substring(currentText.length);
                                const newChars = remainingChars.substring(0, maxCharsPerUpdate);
                                
                                // HTML转义
                                const escapedChars = newChars
                                    .replace(/&/g, '&amp;')
                                    .replace(/</g, '&lt;')
                                    .replace(/>/g, '&gt;');
                                findingTextEl.insertAdjacentHTML('beforeend', escapedChars);
                                console.log(`[流式发现] 第${i + 1}条追加${newChars.length}/${remainingChars.length}字符:`, newChars);
                            }
                        }
                    }
                    
                    if (chatMessages) {
                        scrollToBottom(chatMessages);
                    }

                } catch (e) {
                    console.log('[流式发现] JSON解析未完成，等待更多数据:', e.message);
                }
            } else {
                console.log('[流式发现] 未匹配到keyFindings');
            }
            
            // 尝试提取summary（流式追加）- 改进正则以处理转义字符
            const summaryMatch = partialJson.match(/"summary"\s*:\s*"((?:[^"\\]|\\.)*)"/);
            if (summaryMatch) {
                console.log('[流式总结] 匹配到summary，原始长度:', summaryMatch[1].length);
                
                // 先进行基本的转义处理，再进行unicode解码
                const rawSummary = summaryMatch[1]
                    .replace(/\\n/g, '\n')
                    .replace(/\\"/g, '"')
                    .replace(/\\\\/g, '\\');
                    
                // Unicode解码
                const currentSummary = decodeUnicodeString(rawSummary);
                    
                const lastSummaryLength = parseInt(messageEl.dataset.lastSummaryLength || '0');
                
                if (currentSummary.length > lastSummaryLength) {
                    // 每次最多追加15个字符，实现流式效果
                    const maxCharsPerUpdate = 15;
                    const remainingChars = currentSummary.substring(lastSummaryLength);
                    const newChars = remainingChars.substring(0, maxCharsPerUpdate);
                    
                    console.log(`[流式总结] 追加${newChars.length}/${remainingChars.length}字符:`, newChars);
                    
                    const summaryContainer = messageEl.querySelector('.analysis-summary-container');
                    if (summaryContainer) {
                        // 初始化容器（如果还没有）
                        if (!summaryContainer.querySelector('.summary-container')) {
                            summaryContainer.innerHTML = `
                                <div class="summary-container">
                                    <h4>📝 分析总结：</h4>
                                    <p class="summary-text"></p>
                                </div>
                            `;
                        }
                        
                        const summaryText = summaryContainer.querySelector('.summary-text');
                        if (summaryText) {
                            // 追加新字符，而不是替换全部内容
                            const currentDisplayed = summaryText.textContent || '';
                            summaryText.textContent = currentDisplayed + newChars;
                            messageEl.dataset.lastSummaryLength = (lastSummaryLength + newChars.length).toString();
                            if (chatMessages) {
                                scrollToBottom(chatMessages);
                            }
                        }
                    }
                }
            } else {
                console.log('[流式总结] 未匹配到summary');
            }
        } else {
            console.log('[流式检测] 未检测到CHART_DATA标签');
        }
    }
    
    // 滚动到底部
    if (chatMessages) {
        scrollToBottom(chatMessages);
    }
}

// 绑定任务点击事件的辅助函数
function bindTaskClickEvents(container) {
    const taskItems = container.querySelectorAll('.task-item.expandable');
    taskItems.forEach(item => {
        const header = item.querySelector('.task-header');
        const detail = item.querySelector('.task-detail');
        const expandIcon = item.querySelector('.expand-icon');
        
        if (header && detail && expandIcon) {
            header.style.cursor = 'pointer';
            // 移除旧的事件监听器（如果有）
            const newHeader = header.cloneNode(true);
            header.parentNode.replaceChild(newHeader, header);
            
            // 重新获取元素引用
            const updatedItem = item;
            const updatedHeader = updatedItem.querySelector('.task-header');
            const updatedDetail = updatedItem.querySelector('.task-detail');
            const updatedExpandIcon = updatedItem.querySelector('.expand-icon');
            
            updatedHeader.addEventListener('click', () => {
                const isExpanded = updatedDetail.style.display !== 'none';
                updatedDetail.style.display = isExpanded ? 'none' : 'block';
                updatedExpandIcon.textContent = isExpanded ? '▼' : '▲';
                updatedItem.classList.toggle('expanded', !isExpanded);
            });
        }
    });
}

// 实时更新任务详情（流式过程中调用）
// 注意：detailText 应该已经经过 Unicode 解码
function updateTaskDetailRealtime(container, taskId, detailText, expanded = true) {
    const taskItem = container.querySelector(`[data-task-id="${taskId}"]`);
    if (!taskItem) {
        console.warn(`[updateTaskDetailRealtime] 找不到任务${taskId}`);
        return;
    }
    
    // 查找或创建详情区域
    let detailDiv = taskItem.querySelector('.task-detail');
    let expandIcon = taskItem.querySelector('.expand-icon');
    const taskHeader = taskItem.querySelector('.task-header');
    
    if (!detailDiv) {
        // 第一次创建详情区域
        console.log(`[updateTaskDetailRealtime] 为任务${taskId}创建详情区域`);
        
        // 添加展开图标
        if (!expandIcon && taskHeader) {
            expandIcon = document.createElement('span');
            expandIcon.className = 'expand-icon';
            expandIcon.textContent = expanded ? '▲' : '▼';
            taskHeader.appendChild(expandIcon);
        }
        
        // 创建详情区域
        detailDiv = document.createElement('div');
        detailDiv.className = 'task-detail';
        detailDiv.style.display = expanded ? 'block' : 'none';
        taskItem.appendChild(detailDiv);
        
        // 绑定点击事件
        if (taskHeader) {
            taskHeader.style.cursor = 'pointer';
            taskHeader.addEventListener('click', (e) => {
                e.stopPropagation();
                const currentDetail = taskItem.querySelector('.task-detail');
                const currentExpandIcon = taskItem.querySelector('.expand-icon');
                if (currentDetail && currentExpandIcon) {
                    const isExpanded = currentDetail.style.display !== 'none';
                    currentDetail.style.display = isExpanded ? 'none' : 'block';
                    currentExpandIcon.textContent = isExpanded ? '▼' : '▲';
                    taskItem.classList.toggle('expanded', !isExpanded);
                    console.log(`任务${taskId} ${isExpanded ? '折叠' : '展开'}`);
                }
            });
        }
        
        // 标记为可展开
        taskItem.classList.add('expandable');
        
        // 初始化上次显示的长度
        taskItem.dataset.taskDetailLength = '0';
    }
    
    // 流式更新详情内容（只追加新内容）
    if (detailDiv) {
        const lastLength = parseInt(taskItem.dataset.taskDetailLength || '0');
        
        // 如果新内容更长，说明有更新
        if (detailText.length > lastLength) {
            // 计算需要追加的新内容
            const newContent = detailText.substring(lastLength);
            console.log(`[updateTaskDetailRealtime] 任务${taskId}追加${newContent.length}字符（从${lastLength}到${detailText.length}）`);
            
            // HTML转义新内容
            const escapedNewContent = newContent
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/\n/g, '<br>');
            
            // 追加到现有内容
            detailDiv.insertAdjacentHTML('beforeend', escapedNewContent);
            
            // 更新记录的长度
            taskItem.dataset.taskDetailLength = detailText.length.toString();
        } else if (detailText.length === lastLength) {
            console.log(`[updateTaskDetailRealtime] 任务${taskId}内容未变化，长度:${detailText.length}`);
        } else {
            // 内容变短了（不应该发生），重新渲染
            console.warn(`[updateTaskDetailRealtime] 任务${taskId}内容变短（${lastLength} -> ${detailText.length}），重新渲染`);
            const escapedDetail = detailText
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/\n/g, '<br>');
            detailDiv.innerHTML = escapedDetail;
            taskItem.dataset.taskDetailLength = detailText.length.toString();
        }
        
        // 如果指定了展开状态，应用它
        detailDiv.style.display = expanded ? 'block' : 'none';
        if (expandIcon) {
            expandIcon.textContent = expanded ? '▲' : '▼';
        }
        
        console.log(`[updateTaskDetailRealtime] 任务${taskId}详情更新完成，总长度:${detailText.length}字符，状态:${expanded ? '展开' : '折叠'}`);
    }
}

// 动态更新任务状态（流式过程中调用）
function updateTaskStatus(messageEl, taskId, status) {
    const tasksContainer = messageEl.querySelector('.analysis-tasks-container');
    if (!tasksContainer) {
        console.warn('[updateTaskStatus] 找不到任务容器');
        return;
    }
    
    const taskItem = tasksContainer.querySelector(`[data-task-id="${taskId}"]`);
    if (!taskItem) {
        console.warn(`[updateTaskStatus] 找不到任务${taskId}`);
        return;
    }
    
    const taskIcon = taskItem.querySelector('.task-icon');
    if (!taskIcon) {
        console.warn(`[updateTaskStatus] 找不到任务${taskId}的图标`);
        return;
    }
    
    // 根据状态设置图标
    let statusIcon = '⏳';
    if (status === 'processing') {
        statusIcon = '🔄'; // 处理中（会旋转）
    } else if (status === 'completed') {
        statusIcon = '✅'; // 已完成
    } else if (status === 'failed') {
        statusIcon = '❌'; // 失败
    }
    
    // 只有状态改变时才更新
    const currentIcon = taskIcon.textContent;
    if (currentIcon !== statusIcon) {
        console.log(`[updateTaskStatus] 任务${taskId}: ${currentIcon} → ${statusIcon}`);
        taskIcon.textContent = statusIcon;
        
        // 添加动画效果
        taskIcon.classList.add('task-icon-updated');
        setTimeout(() => {
            taskIcon.classList.remove('task-icon-updated');
        }, 500);
        
        // 更新任务状态类
        const existingClasses = taskItem.className.split(' ').filter(c => !['pending', 'processing', 'completed', 'failed'].includes(c));
        taskItem.className = [...existingClasses, status].join(' ');
        
        console.log(`✓ 任务${taskId}状态已更新为${status} (${statusIcon})`);
    } else {
        console.log(`[updateTaskStatus] 任务${taskId}状态未变化，跳过更新`);
    }
}

// 更新单个任务状态的辅助函数（onComplete时调用）
function updateSingleTask(container, task, taskDetailsMap) {
    const taskItem = container.querySelector(`[data-task-id="${task.taskId}"]`);
    if (!taskItem) {
        console.warn(`找不到任务项: task-${task.taskId}`);
        return;
    }
    
    const taskIcon = taskItem.querySelector('.task-icon');
    if (taskIcon) {
        const statusIcon = task.status === 'completed' ? '✅' : (task.status === 'failed' ? '❌' : '⏳');
        taskIcon.textContent = statusIcon;
        
        // 添加动画效果
        taskIcon.classList.add('task-icon-updated');
        setTimeout(() => {
            taskIcon.classList.remove('task-icon-updated');
        }, 500);
    }
    
    // 添加任务详情（如果有）
    const taskDetail = taskDetailsMap[task.taskId];
    if (taskDetail) {
        const existingDetail = taskItem.querySelector('.task-detail');
        if (!existingDetail) {
            const taskHeader = taskItem.querySelector('.task-header');
            if (!taskHeader) {
                console.warn(`找不到任务头部: task-${task.taskId}`);
                return;
            }
            
            // 添加展开图标
            const expandIcon = document.createElement('span');
            expandIcon.className = 'expand-icon';
            expandIcon.textContent = '▼';
            taskHeader.appendChild(expandIcon);
            
            // 添加任务详情区域
            const detailDiv = document.createElement('div');
            detailDiv.className = 'task-detail';
            detailDiv.style.display = 'none';
            // 转义HTML并保留换行
            const escapedDetail = taskDetail
                .replace(/&/g, '&amp;')
                .replace(/</g, '&lt;')
                .replace(/>/g, '&gt;')
                .replace(/\n/g, '<br>');
            detailDiv.innerHTML = escapedDetail;
            taskItem.appendChild(detailDiv);
            
            // 绑定点击事件
            taskHeader.style.cursor = 'pointer';
            taskHeader.addEventListener('click', (e) => {
                e.stopPropagation(); // 防止事件冒泡
                const currentDetail = taskItem.querySelector('.task-detail');
                const currentExpandIcon = taskItem.querySelector('.expand-icon');
                if (currentDetail && currentExpandIcon) {
                    const isExpanded = currentDetail.style.display !== 'none';
                    currentDetail.style.display = isExpanded ? 'none' : 'block';
                    currentExpandIcon.textContent = isExpanded ? '▼' : '▲';
                    taskItem.classList.toggle('expanded', !isExpanded);
                    console.log(`任务${task.taskId} ${isExpanded ? '折叠' : '展开'}`);
                }
            });
            
            console.log(`✓ 任务${task.taskId}添加了详情和点击事件`);
        }
    }
    
    // 更新任务状态类
    const hasDetail = taskDetail && taskDetail.trim().length > 0;
    taskItem.className = `task-item ${task.status} ${hasDetail ? 'expandable' : ''}`;
    
    console.log(`✓ 任务${task.taskId}最终状态已更新为${task.status}${hasDetail ? '（可展开）' : ''}`);
}

// 更新聊天消息显示
function updateChatMessages() {
    const chatMessages = document.getElementById('chat-messages');
    if (!chatMessages) return;
    
    chatMessages.innerHTML = '';
    
    APP_STATE.currentSession.messages.forEach(message => {
        displayMessage(message.content, message.role);
    });
}

// 发送消息（暴露为全局函数）
window.sendMessage = async function() {
    const messageInput = document.getElementById('message-input');
    const message = messageInput.value.trim();
    
    if (!message) {
        showMessage('请输入消息内容', 'warning');
        return;
    }
    
    // 【修复问题1】检查是否有当前会话，如果没有则提示用户先创建会话
    // 需要更严格的检查：确保 currentSession 存在、有 id、且 id 不为空字符串
    if (!APP_STATE.currentSession || 
        !APP_STATE.currentSession.id || 
        APP_STATE.currentSession.id === '' ||
        APP_STATE.currentSession.id === null ||
        APP_STATE.currentSession.id === undefined) {
        showMessage('请先创建会话后再发送消息。请点击"新建会话"按钮创建聊天会话', 'warning');
        return;
    }
    
    // 禁用发送按钮
    const sendBtn = document.getElementById('send-btn');
    if (!sendBtn) {
        console.error('找不到发送按钮');
        return;
    }
    sendBtn.disabled = true;
    
    try {
        // 再次确认会话对象完整（防止在检查后、执行前被修改）
        if (!APP_STATE.currentSession || !APP_STATE.currentSession.id) {
            showMessage('会话已失效，请重新创建会话', 'warning');
            sendBtn.disabled = false;
            return;
        }
        
        // 确保 messages 数组存在
        if (!APP_STATE.currentSession.messages) {
            APP_STATE.currentSession.messages = [];
        }
        
        // 显示用户消息
        displayMessage(message, 'user');
        
        // 添加到会话消息列表
        APP_STATE.currentSession.messages.push({
            id: generateId('msg_'),
            role: 'user',
            content: message,
            timestamp: Date.now()
        });
        
        // 清空输入框
        messageInput.value = '';
        
        // 生成消息ID用于流式显示
        const messageId = generateId('stream_');
        let streamContent = '';
        
        // 初始化分析状态
        let analysisTitle = '';
        let taskList = [];
        let analysisText = '';
        
        // 【修复】检查是否有文件ID：优先从会话对象中获取，其次从当前文件预览获取
        let currentFileId = null;
        if (APP_STATE.currentSession && APP_STATE.currentSession.fileId) {
            // 优先使用会话对象中保存的文件ID
            currentFileId = APP_STATE.currentSession.fileId;
            console.log('从会话对象获取文件ID:', currentFileId);
        } else if (APP_STATE.currentFile && APP_STATE.currentFile.id) {
            // 如果没有，则使用当前预览的文件ID
            currentFileId = APP_STATE.currentFile.id;
            console.log('从当前文件预览获取文件ID:', currentFileId);
        }
        
        // 【修复】根据是否关联文件来决定chatType
        // 如果有fileId，说明是智能Excel场景；否则是普通聊天场景
        const chatType = currentFileId ? 'excel' : 'plain';
        console.log('发送消息的chatType:', chatType, ', currentFileId:', currentFileId);
        
        // 发送消息到模型
        await ModelAPI.sendMessage(
            APP_STATE.currentSession.id,
            message,
            chatType,  // 【修复】传递chatType参数
            currentFileId, // 如果有文件ID，带上文件ID
            null,  // Excel场景不使用dbConnectId
            null,  // Excel场景不使用databaseName
            null,  // Excel场景不使用tableName
            // onChunk回调
            (chunk) => {
                streamContent += chunk;
                console.log('[onChunk] 收到chunk，长度:', chunk.length, '累计长度:', streamContent.length);
                
                // 检查chunk内容
                if (chunk.includes('CHART_DATA')) {
                    console.log('[onChunk] ✓✓✓ 本次chunk包含CHART_DATA标签！');
                    console.log('[onChunk] chunk前500字符:', chunk.substring(0, 500));
                }
                if (chunk.includes('TITLE_START')) {
                    console.log('[onChunk] ✓ 本次chunk包含TITLE_START');
                }
                if (chunk.includes('TASKS_START')) {
                    console.log('[onChunk] ✓ 本次chunk包含TASKS_START');
                }
                
                // 实时调用displayStreamingMessage更新显示
                console.log('[onChunk] 调用displayStreamingMessage，传入内容长度:', streamContent.length);
                displayStreamingMessage(messageId, streamContent);
                
                console.log('[onChunk] displayStreamingMessage调用完成');
            },
            // onComplete回调
            () => {
                console.log('========== 消息流式传输完成 ==========');
                console.log('streamContent总长度:', streamContent.length);
                console.log('streamContent包含CHART_DATA:', streamContent.includes('<CHART_DATA>'));
                
                // 【修复】检查是否真的收到了数据，如果没有收到数据，不应该显示成功
                if (!streamContent || streamContent.trim() === '') {
                    console.error('========== 消息流式传输完成但未收到任何数据 ==========');
                    console.error('这可能表示后端未收到请求或请求处理失败');
                    showMessage('消息发送失败：未收到后端响应数据', 'error');
                    return;
                }
                
                // 添加到会话消息列表
                APP_STATE.currentSession.messages.push({
                    id: messageId,
                    role: 'assistant',
                    content: streamContent,
                    timestamp: Date.now()
                });

                // 触发消息完成事件，供PreviewManager监听
                document.dispatchEvent(new CustomEvent('messageCompleted', {
                    detail: {
                        content: streamContent,
                        messageId: messageId
                    }
                }));
                
                // 检查是否包含图表数据，如果包含则切换到结果分析tab并渲染
                console.log('========== 开始检查CHART_DATA ==========');
                const chartDataMatch = streamContent.match(/<CHART_DATA>([\s\S]*?)<\/CHART_DATA>/);
                console.log('chartDataMatch结果:', chartDataMatch ? '✓ 找到' : '✗ 未找到');
                
                if (chartDataMatch) {
                    console.log('✓ 检测到图表数据标签！');
                    try {
                        const chartDataJson = chartDataMatch[1];
                        console.log('图表数据JSON长度:', chartDataJson.length);
                        const chartData = safeJSONParse(chartDataJson);
                        if (chartData) {
                            console.log('✓ 解析图表数据成功:', chartData);
                        } else {
                            console.error('✗ 解析图表数据失败（返回null）');
                            throw new Error('JSON解析返回null');
                        }
                        
                        // 检查是否有fileId（修改操作生成的Excel文件，在chartData.data.fileId中）
                        const fileId = (chartData && chartData.data && chartData.data.fileId) ? chartData.data.fileId : null;
                        if (fileId) {
                            console.log('✓ 检测到修改操作生成的Excel文件ID:', fileId);
                            // 在消息中显示文件下载链接
                            setTimeout(() => {
                                const messageEl = document.getElementById(`message-${messageId}`);
                                if (messageEl) {
                                    const fileInfoDiv = document.createElement('div');
                                    fileInfoDiv.className = 'excel-file-info';
                                    fileInfoDiv.style.cssText = 'margin-top: 10px; padding: 10px; background: #e3f2fd; border-radius: 4px; border-left: 4px solid #2196f3;';
                                    fileInfoDiv.innerHTML = `
                                        <p style="margin: 0 0 8px 0; font-weight: bold; color: #1976d2;">
                                            📊 已生成Excel文件
                                        </p>
                                        <p style="margin: 0 0 8px 0; color: #424242; font-size: 14px;">
                                            文件ID: <code style="background: #fff; padding: 2px 6px; border-radius: 3px;">${fileId}</code>
                                        </p>
                                        <button onclick="window.downloadFile('${fileId}')" 
                                                style="background: #2196f3; color: white; border: none; padding: 6px 12px; border-radius: 4px; cursor: pointer; font-size: 14px;">
                                            📥 下载Excel文件
                                        </button>
                                    `;
                                    messageEl.appendChild(fileInfoDiv);
                                }
                            }, 500);
                        }
                        
                        // 注意：总结已经在流式过程中显示，不需要"结果总结中..."的提示
                        // 这里不做任何操作，让流式显示继续
                        const messageEl = document.getElementById(`message-${messageId}`);
                        
                        // 更新聊天消息区域（任务状态完成、折叠）
                        if (messageEl && chartData) {
                            // 只需要折叠所有任务并更新为completed状态
                            // 关键发现和总结已经在流式过程中显示了
                            if (chartData.taskStatus && chartData.taskStatus.length > 0) {
                                taskList = chartData.taskStatus;
                                console.log('✓ 开始更新任务为completed状态，总数:', chartData.taskStatus.length);
                                
                                const tasksContainer = messageEl.querySelector('.analysis-tasks-container');
                                if (tasksContainer) {
                                    // 获取所有任务项
                                    const allTaskItems = tasksContainer.querySelectorAll('.task-item');
                                    console.log('✓ 找到任务项数量:', allTaskItems.length);
                                    
                                    // 更新所有任务为completed并折叠
                                    allTaskItems.forEach((taskItem) => {
                                        const taskId = parseInt(taskItem.dataset.taskId);
                                        console.log(`  处理任务${taskId}...`);
                                        
                                        // 更新状态为completed
                                        const taskIcon = taskItem.querySelector('.task-icon');
                                        if (taskIcon) {
                                            taskIcon.textContent = '✅';
                                            taskIcon.classList.add('task-icon-updated');
                                            setTimeout(() => {
                                                taskIcon.classList.remove('task-icon-updated');
                                            }, 500);
                                        }
                                        
                                        // 更新类名
                                        taskItem.className = taskItem.className
                                            .replace(/\b(pending|processing|failed)\b/g, '')
                                            .trim() + ' completed';
                                        
                                        // 折叠任务详情
                                        const detailDiv = taskItem.querySelector('.task-detail');
                                        const expandIcon = taskItem.querySelector('.expand-icon');
                                        if (detailDiv && expandIcon) {
                                            detailDiv.style.display = 'none';
                                            expandIcon.textContent = '▼';
                                            taskItem.classList.remove('expanded');
                                        }
                                        
                                        console.log(`  ✓ 任务${taskId}已更新为completed并折叠`);
                                    });
                                    
                                    // 确保关键发现和总结已完全显示（如果流式过程中还没显示完）
                                    setTimeout(() => {
                                        // 检查关键发现是否已完全显示
                                        const findingsContainer = messageEl.querySelector('.analysis-findings-container');
                                        if (findingsContainer && chartData.keyFindings) {
                                            const displayedFindings = findingsContainer.querySelectorAll('.finding-item');
                                            const expectedCount = chartData.keyFindings.length;
                                            
                                        if (displayedFindings.length < expectedCount) {
                                            console.log(`⚠️  关键发现显示不完整 (${displayedFindings.length}/${expectedCount})，补充显示`);
                                            
                                            // 初始化容器（如果还没有）
                                            if (!findingsContainer.querySelector('.findings-container')) {
                                                findingsContainer.innerHTML = `
                                                    <div class="findings-container">
                                                        <h4>💡 关键发现：</h4>
                                                        <ul class="findings-list"></ul>
                                                    </div>
                                                `;
                                            }
                                            
                                            const findingsList = findingsContainer.querySelector('.findings-list');
                                            
                                            // 补充缺失的发现（需要unicode解码和HTML转义）
                                            for (let i = displayedFindings.length; i < expectedCount; i++) {
                                                const li = document.createElement('li');
                                                li.className = 'finding-item';
                                                // Unicode解码
                                                const decodedFinding = decodeUnicodeString(chartData.keyFindings[i]);
                                                // HTML转义
                                                const escapedFinding = decodedFinding
                                                    .replace(/&/g, '&amp;')
                                                    .replace(/</g, '&lt;')
                                                    .replace(/>/g, '&gt;');
                                                li.innerHTML = `<span class="finding-icon">💡</span>${escapedFinding}`;
                                                findingsList.appendChild(li);
                                                console.log(`  ✓ 补充显示关键发现${i + 1}`);
                                            }
                                        }
                                        }
                                        
                                        // 检查总结是否已完全显示
                                        const summaryContainer = messageEl.querySelector('.analysis-summary-container');
                                        if (summaryContainer && chartData.summary) {
                                            const summaryText = summaryContainer.querySelector('.summary-text');
                                            
                                            // Unicode解码
                                            const decodedSummary = decodeUnicodeString(chartData.summary);
                                            
                                            if (!summaryText || summaryText.textContent.length < decodedSummary.length) {
                                                const displayedLength = summaryText ? summaryText.textContent.length : 0;
                                                console.log(`⚠️  总结显示不完整 (${displayedLength}/${decodedSummary.length})，补充显示`);
                                                
                                                // 初始化容器（如果还没有）
                                                if (!summaryContainer.querySelector('.summary-container')) {
                                                    summaryContainer.innerHTML = `
                                                        <div class="summary-container">
                                                            <h4>📝 分析总结：</h4>
                                                            <p class="summary-text"></p>
                                                        </div>
                                                    `;
                                                }
                                                
                                                const updatedSummaryText = summaryContainer.querySelector('.summary-text');
                                                if (updatedSummaryText) {
                                                    updatedSummaryText.textContent = decodedSummary;
                                                    console.log('  ✓ 补充显示完整总结');
                                                }
                                            }
                                        }
                                        
                                        console.log('✓ 关键发现和总结检查完成');
                                    }, 300);
                                }
                            }
                        }
                        
                        // 延迟切换标签页，确保DOM已更新
                        setTimeout(() => {
                            console.log('开始切换到结果分析标签页');
                            if (typeof window.switchTab === 'function') {
                                window.switchTab('result-analysis');
                                console.log('✓ 标签页切换完成');
                                
                                // 切换后再更新结果分析区域
                                setTimeout(() => {
                                    if (typeof window.updateResultAnalysis === 'function') {
                                        window.updateResultAnalysis(streamContent, chartData, analysisTitle, taskList);
                                        console.log('✓ 结果分析区域更新完成');
                                    } else {
                                        console.error('✗ updateResultAnalysis 函数不存在！');
                                    }
                                }, 100);
                            } else {
                                console.error('✗ switchTab 函数不存在！');
                            }
                        }, 100);
                        
                    } catch (e) {
                        console.error('✗ 在onComplete中解析图表数据失败:', e);
                        console.error('错误堆栈:', e.stack);
                        console.error('尝试解析的JSON:', chartDataMatch[1]);
                    }
                } else {
                    console.log('✗ 未检测到图表数据标签');
                    console.log('streamContent包含的标签:', streamContent.match(/<[^>]+>/g));
                    console.log('streamContent前1000个字符:', streamContent.substring(0, 1000));
                }
                
                showMessage('消息发送成功', 'success');
            },
            // onError回调
            (error) => {
                console.error('发送消息失败:', error);
                // 检查是否是会话相关的错误
                const errorMessage = error.message || error.toString();
                if (errorMessage.includes('400') || 
                    errorMessage.includes('404') || 
                    errorMessage.includes('session') ||
                    errorMessage.includes('会话')) {
                    showMessage('会话无效或不存在，请先创建会话后再发送消息', 'warning');
                } else {
                    showMessage('消息发送失败: ' + errorMessage, 'error');
                }
            }
        );
        
    } catch (error) {
        console.error('发送消息失败:', error);
        // 检查是否是会话相关的错误
        const errorMessage = error.message || error.toString();
        if (errorMessage.includes('session') || 
            errorMessage.includes('会话') ||
            errorMessage.includes('currentSession')) {
            showMessage('会话无效或不存在，请先创建会话后再发送消息', 'warning');
        } else {
            showMessage('消息发送失败: ' + errorMessage, 'error');
        }
    } finally {
        // 重新启用发送按钮
        sendBtn.disabled = false;
    }
};

// 更新结果分析区域（暴露为全局函数）
window.updateResultAnalysis = function(content, chartData = null, analysisTitle = '', taskList = []) {
    console.log('updateResultAnalysis 被调用');
    console.log('- content长度:', content.length);
    console.log('- chartData:', chartData);
    console.log('- analysisTitle:', analysisTitle);
    console.log('- taskList:', taskList);
    
    const resultContent = document.getElementById('result-content');
    const resultActions = document.getElementById('result-actions');
    
    if (!resultContent) {
        console.error('找不到 result-content 元素');
        return;
    }
    
    // 如果已经有内容，添加分割线
    if (resultContent.children.length > 0) {
        const separator = document.createElement('div');
        separator.className = 'result-separator';
        separator.innerHTML = '<hr><div class="separator-text">新的分析结果</div><hr>';
        resultContent.appendChild(separator);
    }
    
    // 如果没有直接传入图表数据，尝试从内容中提取
    if (!chartData) {
        const chartDataMatch = content.match(/<CHART_DATA>([\s\S]*?)<\/CHART_DATA>/);
        if (chartDataMatch) {
            try {
                const chartDataJson = chartDataMatch[1];
                chartData = safeJSONParse(chartDataJson);
                if (chartData) {
                    console.log('从内容中提取到图表数据:', chartData);
                } else {
                    console.error('解析结果图表数据失败（返回null）');
                }
            } catch (e) {
                console.error('解析结果图表数据失败:', e);
            }
        }
    }
    
    // 创建完整的分析结果面板
    const resultPanel = document.createElement('div');
    resultPanel.className = 'analysis-result-panel';
    
    // 1. 显示标题（优先使用chartData中的title，其次使用传入的analysisTitle）
    const displayTitle = (chartData && chartData.title) || analysisTitle;
    if (displayTitle) {
        const headerDiv = document.createElement('div');
        headerDiv.className = 'result-header';
        // Unicode解码标题
        const decodedTitle = decodeUnicodeString(displayTitle);
        // HTML转义
        const escapedTitle = decodedTitle
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');
        headerDiv.innerHTML = `<h2 class="result-title">📊 ${escapedTitle}</h2>`;
        resultPanel.appendChild(headerDiv);
    }
    
    // 2. 显示总结（如果有）
    if (chartData && chartData.summary) {
        const summarySection = document.createElement('div');
        summarySection.className = 'result-section summary-section';
        // Unicode解码总结
        const decodedSummary = decodeUnicodeString(chartData.summary);
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
    }
    
    // 3. 显示图表（如果有）
    if (chartData && chartData.data) {
        const chartSection = document.createElement('div');
        chartSection.className = 'result-section chart-section';
        chartSection.innerHTML = '<h3 class="section-title">数据可视化</h3>';
        
        const chartContainer = document.createElement('div');
        chartContainer.className = 'chart-result-container';
        const chartId = 'result-chart-' + Date.now();
        chartContainer.id = chartId;
        
        chartSection.appendChild(chartContainer);
        resultPanel.appendChild(chartSection);
        
        // 添加到DOM后再渲染图表
        resultContent.appendChild(resultPanel);
        
        // 延迟渲染图表，确保DOM已完全加载
        setTimeout(() => {
            if (typeof renderChart === 'function') {
                console.log('调用 renderChart 函数');
                renderChart(chartData, chartId);
            } else {
                console.error('renderChart 函数不存在');
            }
        }, 100);
    } else {
        // 没有图表数据，直接添加到DOM
        resultContent.appendChild(resultPanel);
    }
    
    console.log('✓ 分析结果面板渲染完成');
};

// 创建新会话（需要暴露为全局函数供HTML调用）
window.createNewSession = async function() {
    console.log('===== 开始创建新会话 =====');
    
    const modelSelect = document.getElementById('model-select');
    const selectedModel = modelSelect ? modelSelect.value : (CONFIG.DEFAULTS ? CONFIG.DEFAULTS.MODEL : 'deepseek');
    
    console.log('选择的模型:', selectedModel);
    
    try {
        showLoading('创建会话中...');
        
        const response = await ModelAPI.createSession(selectedModel);
        console.log('创建会话响应:', response);
        
        if (response && response.success && response.result) {
            // 【修改】创建新会话时，根据是否有当前预览文件来决定是否保留文件关联
            // 如果有当前预览文件，则在新会话中保持文件关联；如果没有，则完全清空
            const hasCurrentFile = APP_STATE.currentFile && APP_STATE.currentFile.id;
            console.log('创建新会话时检查当前文件:', hasCurrentFile ? APP_STATE.currentFile : '无');

            let newSessionFileId = null; // 新会话的fileId，默认为null

            if (!hasCurrentFile) {
                // 没有当前文件，执行原有逻辑：清空所有内容
                console.log('没有当前文件，执行标准新建会话逻辑');

                // 1. 清空文件预览
                console.log('开始清空文件预览...');
                if (typeof clearFilePreview === 'function') {
                    clearFilePreview();
                    console.log('✓ 文件预览已清空（通过clearFilePreview函数）');
                } else {
                    // 如果没有clearFilePreview函数，手动清空
                    console.log('clearFilePreview函数不存在，手动清空文件预览');
                    const uploadPrompt = document.getElementById('upload-prompt');
                    const excelPreview = document.getElementById('excel-preview');
                    const excelTable = document.getElementById('excel-table');
                    const sheetTabs = document.getElementById('sheet-tabs');
                    const excelFileName = document.getElementById('excel-file-name');

                    if (uploadPrompt) {
                        uploadPrompt.style.display = '';
                        console.log('✓ upload-prompt 已显示');
                    }
                    if (excelPreview) {
                        excelPreview.style.display = 'none';
                        console.log('✓ excel-preview 已隐藏');
                    }
                    if (excelTable) {
                        excelTable.innerHTML = '';
                        console.log('✓ excel-table 已清空');
                    }
                    if (sheetTabs) {
                        sheetTabs.innerHTML = '';
                        console.log('✓ sheet-tabs 已清空');
                    }
                    if (excelFileName) {
                        excelFileName.textContent = '';
                        console.log('✓ excel-file-name 已清空');
                    }
                    console.log('✓ 文件预览已手动清空');
                }

                // 2. 清空聊天消息显示
                const chatMessages = document.getElementById('chat-messages');
                if (chatMessages) {
                    chatMessages.innerHTML = '';
                    console.log('聊天消息已清空');
                }

                // 3. 清空结果分析
                const resultContent = document.getElementById('result-content');
                if (resultContent) {
                    resultContent.innerHTML = '';
                    console.log('结果分析已清空');
                }

                // 清空文件状态
                APP_STATE.currentFile = null;
                console.log('✓ APP_STATE.currentFile 已清空');
                newSessionFileId = null;

            } else {
                // 有当前文件，保留文件预览，只清空聊天消息和结果分析
                console.log('有当前文件，保留文件预览，只清空聊天消息');

                // 1. 保留文件预览，不清空
                console.log('✓ 保留文件预览');

                // 2. 清空结果分析
                const resultContent = document.getElementById('result-content');
                if (resultContent) {
                    resultContent.innerHTML = '';
                    console.log('结果分析已清空');
                }

                // 3. 清空聊天消息显示
                const chatMessages = document.getElementById('chat-messages');
                if (chatMessages) {
                    chatMessages.innerHTML = '';
                    console.log('聊天消息已清空');
                } else {
                    console.warn('找不到 chat-messages 元素');
                }

                // 新会话不关联文件，上传按钮将被激活
                newSessionFileId = null;
                console.log('新会话不关联文件，上传按钮将被激活');
            }

            // 更新当前会话状态
            const newSession = {
                id: response.result.chatSessionId,
                model: response.result.modelName,
                createdAt: Date.now(),
                messages: [],
                fileId: newSessionFileId,
                type: 'excel'
            };

            // 【修改】使用SessionManager设置Excel会话
            if (typeof SessionManager !== 'undefined' && SessionManager.setExcelSession) {
                SessionManager.setExcelSession(newSession);
                console.log('✓ 已通过SessionManager设置Excel会话');
            } else {
                // 降级到直接设置（向后兼容）
                APP_STATE.currentSession = newSession;
                console.log('⚠ SessionManager不可用，使用降级方案设置会话');
            }
            
            console.log('========== 新会话已创建 ==========');
            console.log('当前会话状态:', APP_STATE.currentSession);
            console.log('会话ID:', APP_STATE.currentSession.id);
            console.log('会话fileId:', APP_STATE.currentSession.fileId);
            console.log('新会话不继承之前的文件关联');
            console.log('===================================');
            
            // 添加到会话列表
            APP_STATE.sessions.unshift({
                id: response.result.chatSessionId,
                model: response.result.modelName,
                created_at: Date.now(),
                updated_at: Date.now(),
                message_count: 0,
                first_user_message: ''
            });
            
            // 如果新会话有关联文件，自动绑定文件到会话
            if (newSessionFileId) {
                try {
                    console.log('自动绑定文件到新会话:', newSessionFileId, response.result.chatSessionId);
                    const bindResponse = await FileAPI.handleFileChatSessionMap(newSessionFileId, response.result.chatSessionId);
                    if (bindResponse && bindResponse.success) {
                        console.log('✓ 文件绑定成功');
                    } else {
                        console.warn('⚠ 文件绑定失败，但不影响会话创建');
                    }
                } catch (error) {
                    console.warn('⚠ 文件绑定异常，但不影响会话创建:', error);
                }
            }

            // 更新会话列表（如果函数存在）
            if (typeof updateSessionsList === 'function') {
                updateSessionsList();
            }

            // 启用发送按钮
            const sendBtn = document.getElementById('send-btn');
            if (sendBtn) {
                sendBtn.disabled = false;
                console.log('发送按钮已启用');
            } else {
                console.warn('找不到 send-btn 元素');
            }
            
            // 【重要】启用上传按钮（会话创建成功后，新会话没有关联文件，应该启用）
            // 确保在更新上传按钮状态前，会话状态已完全设置
            if (typeof updateUploadButtonState === 'function') {
                console.log('准备更新上传按钮状态（新会话创建成功）');
                console.log('当前会话状态检查:', {
                    hasSession: !!(APP_STATE && APP_STATE.currentSession),
                    sessionId: APP_STATE && APP_STATE.currentSession ? APP_STATE.currentSession.id : 'N/A',
                    fileId: APP_STATE && APP_STATE.currentSession ? APP_STATE.currentSession.fileId : 'N/A'
                });
                updateUploadButtonState();
            } else {
                console.warn('updateUploadButtonState 函数不存在');
            }
            
            // 不显示成功提示，避免重复（API层可能已经显示）
            console.log('会话创建流程完成');
        } else {
            console.error('创建会话响应格式错误:', response);
            showMessage('创建会话失败：响应格式错误', 'error');
        }
        
    } catch (error) {
        console.error('创建会话失败:', error);
        showMessage('创建会话失败：' + (error.message || '未知错误'), 'error');
    } finally {
        hideLoading();
    }
};

// 初始化聊天功能（暴露为全局函数供 main.js 调用）
window.initChat = function() {
    console.log('initChat: 开始初始化聊天功能');
    
    // 发送按钮事件
    const sendBtn = document.getElementById('send-btn');
    if (sendBtn) {
        sendBtn.addEventListener('click', sendMessage);
        // 确保发送按钮在初始化时是启用的
        sendBtn.disabled = false;
        console.log('initChat: 发送按钮事件已绑定');
    } else {
        console.error('initChat: 找不到 send-btn 元素');
    }
    
    // 消息输入框回车事件
    const messageInput = document.getElementById('message-input');
    if (messageInput) {
        messageInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                sendMessage();
            }
        });
        console.log('initChat: 消息输入框事件已绑定');
    } else {
        console.error('initChat: 找不到 message-input 元素');
    }
    
    // 新建会话按钮事件
    const newSessionBtn = document.getElementById('new-session-btn');
    console.log('initChat: 查找新建会话按钮, 结果:', newSessionBtn);
    if (newSessionBtn) {
        // 使用全局的 window.createNewSession
        newSessionBtn.addEventListener('click', () => {
            console.log('新建会话按钮被点击（通过事件监听器）');
            if (typeof window.createNewSession === 'function') {
                window.createNewSession();
            } else {
                console.error('window.createNewSession 函数不存在');
            }
        });
        console.log('initChat: 新建会话按钮事件已绑定');
        
        // 测试：直接点击按钮
        console.log('initChat: 测试按钮点击能力');
        console.log('  - 按钮样式:', window.getComputedStyle(newSessionBtn).pointerEvents);
        console.log('  - 按钮是否可见:', newSessionBtn.offsetParent !== null);
        console.log('  - 按钮位置:', newSessionBtn.getBoundingClientRect());
    } else {
        console.error('initChat: 找不到 new-session-btn 元素');
    }
    
    // 结果分析区域按钮事件
    const resultActions = document.getElementById('result-actions');
    if (resultActions) {
        resultActions.addEventListener('click', (e) => {
            const target = e.target.closest('.action-btn');
            if (!target) return;
            
            if (target.classList.contains('preview-btn')) {
                // 预览功能
                showMessage('预览功能开发中', 'info');
            } else if (target.classList.contains('download-btn')) {
                // 下载功能
                downloadResult();
            } else if (target.classList.contains('email-btn')) {
                // 邮箱功能
                showMessage('邮箱功能开发中', 'info');
            }
        });
    }
};

// 下载结果（暴露为全局函数）
window.downloadResult = async function() {
    const resultContent = document.getElementById('result-content');
    if (!resultContent) return;
    
    try {
        // 获取纯文本内容
        const textContent = resultContent.textContent || resultContent.innerText;
        
        // 创建Blob对象
        const blob = new Blob([textContent], { type: 'text/plain;charset=utf-8' });
        
        // 创建下载链接
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `Chat2Excel_结果_${new Date().getTime()}.txt`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        
        showMessage('结果下载成功', 'success');
    } catch (error) {
        console.error('下载结果失败:', error);
        showMessage('下载结果失败', 'error');
    }
};

// 页面切换功能
function switchPage(pageName) {
    // 隐藏所有页面
    document.querySelectorAll('.page').forEach(page => {
        page.classList.remove('active');
    });
    
    // 显示目标页面
    const targetPage = document.getElementById(`${pageName}-page`);
    if (targetPage) {
        targetPage.classList.add('active');
    }
    
    // 更新导航项状态
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.remove('active');
    });
    
    const targetNav = document.querySelector(`[data-page="${pageName}"]`);
    if (targetNav) {
        targetNav.classList.add('active');
    }
    
    // 更新状态
    APP_STATE.ui.currentPage = pageName;
    
    // 延迟加载：只在切换到相应页面时才从服务器获取数据
    if (pageName === 'files') {
        // 当切换到"我的文件"页面时，获取文件列表
        updateFilesList();
    } else if (pageName === 'history') {
        // 当切换到"历史会话"页面时，获取会话列表
        updateSessionsList();
    }
}

// 标签页切换功能
// 注意：switchTab 函数已在 main.js 中定义

// 测试函数：手动触发新建会话（用于调试）
window.testCreateSession = function() {
    console.log('手动测试：创建会话');
    const btn = document.getElementById('new-session-btn');
    console.log('按钮元素:', btn);
    console.log('createNewSession 函数:', typeof window.createNewSession);
    
    if (typeof window.createNewSession === 'function') {
        window.createNewSession();
    } else {
        console.error('createNewSession 函数不存在！');
    }
};

// 验证所有必要的全局函数
window.verifyChatFunctions = function() {
    const functions = [
        'initChat',
        'sendMessage',
        'createNewSession',
        'downloadResult',
        'displayMessage',
        'displayStreamingMessage',
        'updateResultAnalysis',
        'updateChatMessages'
    ];
    
    console.log('=== 验证 chat.js 函数 ===');
    functions.forEach(funcName => {
        const exists = typeof window[funcName] === 'function' || typeof eval(funcName) === 'function';
        console.log(`${funcName}: ${exists ? '✓' : '✗'}`);
    });
};

// 注意：initPageNavigation 已在 main.js 中定义，这里不再重复定义
// 注意：页面初始化由 main.js 统一管理

// 调试函数：检查最后一条消息的内容
window.debugLastMessage = function() {
    if (!APP_STATE.currentSession || !APP_STATE.currentSession.messages || APP_STATE.currentSession.messages.length === 0) {
        console.log('当前没有消息');
        return;
    }
    
    const lastMessage = APP_STATE.currentSession.messages[APP_STATE.currentSession.messages.length - 1];
    const content = lastMessage.content;
    
    console.log('========== 最后一条消息调试信息 ==========');
    console.log('消息ID:', lastMessage.id);
    console.log('消息角色:', lastMessage.role);
    console.log('内容长度:', content.length);
    console.log('包含CHART_DATA标签:', content.includes('<CHART_DATA>'));
    
    // 尝试解析JSON格式
    const jsonMatch = content.match(/\{[\s\S]*?"title"[\s\S]*?"tasks"[\s\S]*?"analysis"[\s\S]*?"sql"[\s\S]*?\}/);
    if (jsonMatch) {
        console.log('✓ 检测到JSON格式的分析计划');
        try {
            const planJson = JSON.parse(jsonMatch[0]);
            console.log('  - title:', planJson.title);
            console.log('  - tasks:', planJson.tasks ? planJson.tasks.length + '个任务' : '无');
            console.log('  - analysis:', planJson.analysis ? planJson.analysis.length + '条分析' : '无');
            console.log('  - sql:', planJson.sql ? '有' : '无');
        } catch (e) {
            console.log('✗ JSON解析失败:', e.message);
        }
    } else {
        console.log('✗ 未检测到JSON格式的分析计划');
    }
    
    // 尝试提取CHART_DATA
    const chartDataMatch = content.match(/<CHART_DATA>([\s\S]*?)<\/CHART_DATA>/);
    if (chartDataMatch) {
        console.log('✓ 找到CHART_DATA标签');
        console.log('CHART_DATA内容长度:', chartDataMatch[1].length);
        console.log('CHART_DATA前200字符:', chartDataMatch[1].substring(0, 200));
        
        try {
            const chartData = safeJSONParse(chartDataMatch[1]);
            if (chartData) {
                console.log('✓ CHART_DATA解析成功');
            } else {
                console.error('✗ CHART_DATA解析失败（返回null）');
            }
            console.log('  - taskStatus:', chartData.taskStatus ? chartData.taskStatus.length + '个任务' : '无');
            console.log('  - keyFindings:', chartData.keyFindings ? chartData.keyFindings.length + '条' : '无');
            console.log('  - summary:', chartData.summary ? '有' : '无');
            console.log('  - chartType:', chartData.chartType);
            console.log('  - data:', chartData.data ? '有' : '无');
        } catch (e) {
            console.error('✗ CHART_DATA解析失败:', e);
        }
    } else {
        console.log('✗ 未找到CHART_DATA标签');
        console.log('最后1000个字符:', content.slice(-1000));
    }
    
    console.log('========================================');
};

console.log('chat.js 已加载');
console.log('可用测试命令:');
console.log('  - testCreateSession() : 测试创建会话');
console.log('  - verifyChatFunctions() : 验证所有函数');
console.log('  - debugLastMessage() : 调试最后一条消息（检查CHART_DATA）');