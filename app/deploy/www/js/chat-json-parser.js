/**
 * chat-json-parser.js
 * 
 * 根据提示词模板格式，解析模型返回的标签式响应
 */

// 使用全局safeJSONParse函数（由main.js定义）
// 如果main.js还未加载，则创建一个临时实现
if (typeof window.safeJSONParse === 'undefined') {
    console.warn('[chat-json-parser.js] safeJSONParse未定义，请确保main.js已加载');
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

/**
 * chat-json-parser.js
 * 
 * 根据提示词模板格式，解析模型返回的标签式响应
 * 
 * 提示词定义的输出格式（使用XML风格标签）：
 * 
 * 第一阶段 - 分析计划（四个阶段标签）：
 * <TITLE_START>
 * 分析标题
 * <TITLE_END>
 * 
 * <TASKS_START>
 * 1. 任务描述1
 * 2. 任务描述2
 * ...
 * <TASKS_END>
 * 
 * <ANALYSIS_START>
 * 任务1的实现思路
 * 任务2的实现思路
 * ...
 * <ANALYSIS_END>
 * 
 * <SQL_START>
 * <api-call><name>response_table</name><args><sql>
 * SELECT ...
 * </sql></args></api-call>
 * <SQL_END>
 * 
 * 第二阶段 - 总结JSON（后端用<CHART_DATA>标签包装）：
 * <CHART_DATA>
 * {
 *   "taskStatus": [{"taskId": 1, "description": "任务描述", "status": "completed"}],
 *   "keyFindings": ["发现1", "发现2"],
 *   "summary": "总结文本",
 *   "chartType": "BarChart",
 *   "chartConfig": {"title": "图表标题", "xAxis": 0, "yAxis": 1},
 *   "data": { ... }
 * }
 * </CHART_DATA>
 */

/**
 * 解析流式内容中的标签式内容（支持实时流式解析）
 * @param {string} content - 流式接收的内容
 * @returns {Object} { firstPhase: null|Object, summaryPhase: null|Object }
 */
function parseStreamingJSON(content) {
    console.log('[parseStreamingJSON] ========== 开始解析内容 ==========');
    console.log('[parseStreamingJSON] 内容总长度:', content.length);
    console.log('[parseStreamingJSON] 前200字符:', content.substring(0, 200));
    
    const result = {
        firstPhase: null,    // 分析计划（从标签中解析）
        summaryPhase: null   // 总结JSON
    };
    
    // ========== 第一阶段：解析四个标签（TITLE, TASKS, ANALYSIS, SQL） ==========
    
    // 1. 解析 <TITLE_START>...<TITLE_END> (支持不完整标签的流式解析)
    let title = null;
    const titleStartPos = content.indexOf('<TITLE_START>');
    if (titleStartPos !== -1) {
        const titleContentStart = titleStartPos + 13; // '<TITLE_START>'.length
        const titleEndPos = content.indexOf('<TITLE_END>', titleContentStart);
        
        if (titleEndPos !== -1) {
            // 标签完整，提取完整标题
            title = content.substring(titleContentStart, titleEndPos).trim();
            console.log('[parseStreamingJSON] TITLE解析完成:', title);
        } else {
            // 标签未闭合，提取当前已有内容（实时显示）
            const partialTitle = content.substring(titleContentStart).trim();
            if (partialTitle && !partialTitle.includes('<TASKS_START>')) {
                title = partialTitle;
                console.log('[parseStreamingJSON] TITLE部分内容（流式）:', title.substring(0, 50));
            }
        }
    }
    
    // 2. 解析 <TASKS_START>...<TASKS_END> (支持流式解析)
    let tasks = null;
    const tasksStartPos = content.indexOf('<TASKS_START>');
    if (tasksStartPos !== -1) {
        const tasksContentStart = tasksStartPos + 13; // '<TASKS_START>'.length
        const tasksEndPos = content.indexOf('<TASKS_END>', tasksContentStart);
        
        let tasksText = '';
        if (tasksEndPos !== -1) {
            // 标签完整
            tasksText = content.substring(tasksContentStart, tasksEndPos).trim();
            console.log('[parseStreamingJSON] TASKS解析完成，原始文本长度:', tasksText.length);
            console.log('[parseStreamingJSON] TASKS原始文本（前200字符）:', tasksText.substring(0, 200));
        } else {
            // 标签未闭合，提取当前已有内容（实时显示）
            const partialContent = content.substring(tasksContentStart);
            console.log('[parseStreamingJSON] TASKS部分原始内容:', partialContent);
            
            // 查找下一个标签的位置
            const nextTagPos = partialContent.search(/<ANALYSIS_START>|<SQL_START>/);
            if (nextTagPos !== -1) {
                tasksText = partialContent.substring(0, nextTagPos).trim();
            } else {
                tasksText = partialContent.trim();
            }
            console.log('[parseStreamingJSON] TASKS部分内容（流式），长度:', tasksText.length);
            console.log('[parseStreamingJSON] TASKS部分内容（前200字符）:', tasksText.substring(0, 200));
        }
        
        if (tasksText) {
            tasks = [];
            
            // 【重要修复】针对用户提供的数据格式进行专门处理
            console.log('[parseStreamingJSON] 开始解析任务文本:', tasksText);
            
            // 特殊处理：对于没有换行符的任务列表，按数字分割
            if (!tasksText.includes('\n')) {
                console.log('[parseStreamingJSON] 检测到无换行符的任务列表格式');
                
                // 流式处理改进：检查是否可能是完整的任务列表
                // 如果第一个任务都不完整，说明数据太少，暂缓解析
                const isFirstTaskComplete = tasksText.match(/^\d+\./);
                
                if (!isFirstTaskComplete) {
                    console.log('[parseStreamingJSON] 数据不完整，暂缓解析任务列表');
                    tasks = []; // 返回空任务列表，等待更多数据
                } else {
                    // 使用更精确的正则表达式匹配所有任务
                    // 先尝试移除可能意外包含的标签
                    let cleanTasksText = tasksText.replace('</TASKS_START>', '').replace('</TASKS_END>', '');
                    
                    // 【关键修复】使用改进的正则表达式匹配所有任务
                    // 修复说明：
                    // 1. 使用更可靠的方法：先找到所有任务开始位置，然后逐个提取完整描述
                    // 2. 确保每个任务的描述都是完整的，不会被截断
                    // 3. 添加详细的调试日志以便排查问题
                    console.log('[parseStreamingJSON] 开始解析任务文本，长度:', cleanTasksText.length);
                    console.log('[parseStreamingJSON] 任务文本内容:', cleanTasksText);
                    
                    const taskMatches = [];
                    const taskStartRegex = /\d+\./g;
                    let startMatch;
                    
                    // 第一步：找到所有任务开始位置
                    while ((startMatch = taskStartRegex.exec(cleanTasksText)) !== null) {
                        const taskIdStr = startMatch[0]; // 例如 "1." 或 "12."
                        const taskId = parseInt(taskIdStr);
                        taskMatches.push({
                            start: startMatch.index,
                            taskId: taskId,
                            taskIdStr: taskIdStr
                        });
                        console.log(`[parseStreamingJSON] 找到任务${taskId}开始位置: ${startMatch.index}, 格式: "${taskIdStr}"`);
                    }
                    
                    console.log(`[parseStreamingJSON] 共找到${taskMatches.length}个任务开始位置`);
                    
                    // 第二步：提取每个任务的完整描述
                    for (let i = 0; i < taskMatches.length; i++) {
                        const currentMatch = taskMatches[i];
                        const nextMatch = taskMatches[i + 1];
                        
                        // 计算任务描述的起始位置：跳过 "数字." 部分
                        const descStart = currentMatch.start + currentMatch.taskIdStr.length;
                        // 计算任务描述的结束位置：下一个任务开始位置，或者文本末尾
                        const descEnd = nextMatch ? nextMatch.start : cleanTasksText.length;
                        
                        let taskDescription = cleanTasksText.substring(descStart, descEnd).trim();
                        
                        console.log(`[parseStreamingJSON] 任务${currentMatch.taskId}描述范围: [${descStart}, ${descEnd}), 长度: ${taskDescription.length}`);
                        console.log(`[parseStreamingJSON] 任务${currentMatch.taskId}原始描述: "${taskDescription}"`);
                        
                        // 清理任务描述中的标签（防止意外包含）
                        taskDescription = taskDescription.replace('</TASKS_START>', '').replace('</TASKS_END>', '').trim();
                        
                        // 【关键修复】确保任务描述完整：如果这是最后一个任务且标签未闭合，保留描述（即使可能不完整）
                        // 但如果标签已闭合，则必须确保描述完整
                        const isLastTask = (i === taskMatches.length - 1);
                        const isTagsComplete = (tasksEndPos !== -1);
                        
                        // 过滤掉明显不完整的任务（但最后一个任务在标签完整时必须保留）
                        if (taskDescription.length > 0) {
                            // 如果是最后一个任务且标签未闭合，检查描述是否太短（可能是流式传输中不完整）
                            if (isLastTask && !isTagsComplete && taskDescription.length < 5) {
                                console.log(`[parseStreamingJSON] 最后一个任务描述可能不完整（流式），暂不添加: "${taskDescription}"`);
                                // 不添加到任务列表，等待更多数据
                            } else {
                                tasks.push({
                                    id: currentMatch.taskId,
                                    description: taskDescription
                                });
                                
                                console.log(`[parseStreamingJSON] ✓ 解析任务${currentMatch.taskId}成功，描述长度: ${taskDescription.length}, 内容: "${taskDescription}"`);
                            }
                        } else {
                            console.warn(`[parseStreamingJSON] ⚠️ 任务${currentMatch.taskId}描述为空，跳过`);
                        }
                    }
                    
                    // 流式处理改进：如果最后一个任务看起来不完整，移除它
                    // 但在标签完整的情况下，不应该移除最后一个任务
                    if (tasks.length > 0 && tasksEndPos === -1) {
                        const lastTask = tasks[tasks.length - 1];
                        // 使用清理后的任务描述进行启发式检查：如果最后一个任务描述很短，可能是不完整的
                        // 只有在流式传输且任务描述非常短的情况下才移除
                        if (lastTask.description.length < 3) {
                            console.log('[parseStreamingJSON] 最后一个任务可能不完整，暂时移除:', lastTask);
                            tasks.pop(); // 移除可能不完整的最后一个任务
                        }
                    }
                    
                    // 【额外修复】如果正则表达式没有匹配到所有任务，尝试备用方法
                    // 检查是否所有预期的任务都被解析（通过检查任务ID的连续性）
                    if (tasks.length > 0) {
                        const expectedTaskCount = tasks[tasks.length - 1].id;
                        if (tasks.length < expectedTaskCount) {
                            console.warn(`[parseStreamingJSON] ⚠️ 警告：只解析到${tasks.length}个任务，但最后一个任务ID是${expectedTaskCount}`);
                            console.warn(`[parseStreamingJSON] 原始任务文本:`, cleanTasksText);
                        }
                    }
                }
            } else {
                // 有换行符的情况，使用原有按行处理的方法
                console.log('[parseStreamingJSON] 使用按行处理方法');
                
                const lines = tasksText.split('\n');
                console.log('[parseStreamingJSON] 任务文本行数:', lines.length);
                
                let currentTask = null;
                
                lines.forEach(line => {
                    line = line.trim();
                    if (!line) return;
                    
                    // 检查这行是否是新任务的开始（格式: 数字. 任务描述）
                    const taskMatch = line.match(/^(\d+)\.\s*(.*)$/);
                    
                    if (taskMatch) {
                        // 如果已有未完成的任务，先添加到结果中
                        if (currentTask) {
                            tasks.push(currentTask);
                            console.log(`[parseStreamingJSON] 添加任务${currentTask.id}: ${currentTask.description}`);
                        }
                        
                        // 创建新任务
                        const taskId = parseInt(taskMatch[1]);
                        let taskDesc = taskMatch[2].trim();
                        
                        currentTask = {
                            id: taskId,
                            description: taskDesc
                        };
                    } else if (currentTask) {
                        // 如果不是新任务，且有当前任务，则将该行添加到当前任务描述中
                        currentTask.description += ' ' + line;
                    }
                });
                
                // 不要忘记最后一个任务
                if (currentTask) {
                    tasks.push(currentTask);
                    console.log(`[parseStreamingJSON] 添加最后一个任务${currentTask.id}: ${currentTask.description}`);
                }
            }
            
            // 额外的安全检查：确保任务描述不为空
            tasks = tasks.filter(task => task.description.trim() !== '');
            
            console.log('[parseStreamingJSON] 解析出的任务数:', tasks.length);
            console.log('[parseStreamingJSON] 任务详情:', tasks);
        } else {
            console.warn('[parseStreamingJSON] ⚠️ tasksText为空或无法提取任务！');
            console.warn('[parseStreamingJSON] tasksText内容:', tasksText);
        }
    } else {
        console.warn('[parseStreamingJSON] ⚠️ 未找到TASKS_START标签！');
    }
    
    // 3. 解析 <ANALYSIS_START>...<ANALYSIS_END> (支持流式解析，逐条提取)
    let analysis = null;
    const analysisStartPos = content.indexOf('<ANALYSIS_START>');
    if (analysisStartPos !== -1) {
        const analysisContentStart = analysisStartPos + 16; // '<ANALYSIS_START>'.length
        const analysisEndPos = content.indexOf('<ANALYSIS_END>', analysisContentStart);
        
        let analysisText = '';
        if (analysisEndPos !== -1) {
            // 标签完整
            analysisText = content.substring(analysisContentStart, analysisEndPos).trim();
            console.log('[parseStreamingJSON] ANALYSIS解析完成，长度:', analysisText.length);
            console.log('[parseStreamingJSON] ANALYSIS前200字符:', analysisText.substring(0, 200));
        } else {
            // 标签未闭合，提取当前已有内容（实时显示）
            const partialContent = content.substring(analysisContentStart);
            const nextTagPos = partialContent.search(/<SQL_START>/);
            if (nextTagPos !== -1) {
                analysisText = partialContent.substring(0, nextTagPos).trim();
            } else {
                analysisText = partialContent.trim();
            }
            console.log('[parseStreamingJSON] ANALYSIS部分内容（流式），长度:', analysisText.length);
            console.log('[parseStreamingJSON] ANALYSIS前200字符:', analysisText.substring(0, 200));
        }
        
        if (analysisText) {
            analysis = [];
            
            // 尝试通过"任务1"、"任务2"等关键词分段
            // 使用正则查找所有"任务N"的位置
            const taskMatches = [...analysisText.matchAll(/任务(\d+)[：:、]/g)];
            
            if (taskMatches.length > 0) {
                // 有明确的任务标记，按任务分段
                for (let i = 0; i < taskMatches.length; i++) {
                    const match = taskMatches[i];
                    const taskId = parseInt(match[1]);
                    const startPos = match.index;
                    const endPos = (i + 1 < taskMatches.length) ? taskMatches[i + 1].index : analysisText.length;
                    
                    const content = analysisText.substring(startPos, endPos).trim();
                    if (content) {
                        analysis.push({
                            taskId: taskId,
                            content: content
                        });
                    }
                }
            } else {
                // 没有明确的任务标记，尝试按段落分割
                const paragraphs = analysisText.split(/\n\n+/);
                paragraphs.forEach((para, index) => {
                    const trimmedPara = para.trim();
                    if (trimmedPara) {
                        analysis.push({
                            taskId: index + 1,
                            content: trimmedPara
                        });
                    }
                });
                
                // 如果连段落都没有，整段作为一个分析
                if (analysis.length === 0 && analysisText) {
                    analysis.push({
                        taskId: 1,
                        content: analysisText
                    });
                }
            }
            
            console.log('[parseStreamingJSON] 解析出的分析段数:', analysis.length);
        } else {
            console.warn('[parseStreamingJSON] ⚠️ analysisText为空！');
        }
    } else {
        console.warn('[parseStreamingJSON] ⚠️ 未找到ANALYSIS_START标签！');
    }
    
    // 4. 解析 <SQL_START>...<SQL_END>
    let sql = null;
    const sqlStartPos = content.indexOf('<SQL_START>');
    if (sqlStartPos !== -1) {
        const sqlContentStart = sqlStartPos + 11; // '<SQL_START>'.length
        const sqlEndPos = content.indexOf('<SQL_END>', sqlContentStart);
        
        if (sqlEndPos !== -1) {
            const sqlContent = content.substring(sqlContentStart, sqlEndPos);
            // 提取 <api-call>...<sql>...<args>
            const apiCallMatch = sqlContent.match(/<api-call>.*?<args><sql>([\s\S]*?)<\/sql><\/args><\/api-call>/);
            if (apiCallMatch) {
                sql = apiCallMatch[1].trim();
            } else {
                // 如果没有api-call标签，直接提取SQL内容
                sql = sqlContent.trim();
            }
            console.log('[parseStreamingJSON] SQL解析完成');
        }
    }
    
    // 组装第一阶段结果
    if (title || tasks || analysis || sql) {
        result.firstPhase = {
            title: title,
            tasks: tasks,
            analysis: analysis,
            sql: sql
        };
        
        console.log('[标签解析器] 第一阶段解析结果:');
        console.log('  - title:', title ? '已提取' : '未找到');
        console.log('  - tasks:', tasks ? `${tasks.length}个任务` : '未找到');
        console.log('  - analysis:', analysis ? `${analysis.length}个分析段` : '未找到');
        console.log('  - sql:', sql ? '已提取' : '未找到');
    }
    
    // ========== 第二阶段：流式解析总结JSON（模型返回的纯JSON，边累积边解析） ==========
    console.log('[parseStreamingJSON] 开始流式解析总结JSON...');

    // 直接累积content中的纯JSON内容（没有标签包裹）
    let summaryJsonText = content;
    
    if (summaryJsonText) {
        console.log('[parseStreamingJSON] ✓ 累积JSON内容长度:', summaryJsonText.length);
        console.log('[parseStreamingJSON] 累积内容前200字符:', summaryJsonText.substring(0, 200));
        
        // 【关键修复】始终尝试流式部分解析，即使JSON不完整也能实时显示
        const partialResult = {};
        
        // 1. 首先尝试完整解析JSON
        let jsonComplete = false;
        try {
            result.summaryPhase = JSON.parse(summaryJsonText);
            console.log('[parseStreamingJSON] ✓✓✓ 成功解析完整总结JSON');
            jsonComplete = true;
        } catch (e) {
            console.log('[parseStreamingJSON] JSON未完整，进行流式部分解析...');
        }
        
        // 2. 如果JSON不完整，进行流式部分解析（实时提取已有字段）
        if (!jsonComplete) {
            // 提取taskStatus数组（流式）- 优先解析，因为它会更新任务状态
            // 【修复】使用贪婪匹配，确保捕获完整数组
            const taskStatusMatch = summaryJsonText.match(/"taskStatus"\s*:\s*\[([^\]]*)\]/);
            if (taskStatusMatch) {
                try {
                    const taskStatusArray = JSON.parse('[' + taskStatusMatch[1] + ']');
                    partialResult.taskStatus = taskStatusArray;
                    console.log('[parseStreamingJSON] ✓ 流式解析taskStatus:', taskStatusArray.length, '条');
                } catch (e2) {
                    // 数组不完整，尝试逐个提取对象
                    console.log('[parseStreamingJSON] taskStatus数组不完整，尝试逐个提取...');
                    const taskObjects = [];
                    const taskObjRegex = /\{\s*"taskId"\s*:\s*(\d+)\s*,\s*"description"\s*:\s*"([^"]*)"\s*,\s*"status"\s*:\s*"([^"]*)"\s*\}/g;
                    let taskMatch;
                    while ((taskMatch = taskObjRegex.exec(summaryJsonText)) !== null) {
                        taskObjects.push({
                            taskId: parseInt(taskMatch[1]),
                            description: taskMatch[2],
                            status: taskMatch[3]
                        });
                    }
                    if (taskObjects.length > 0) {
                        partialResult.taskStatus = taskObjects;
                        console.log('[parseStreamingJSON] ✓ 流式逐个提取taskStatus:', taskObjects.length, '条');
                    }
                }
            }
            
            // 提取keyFindings数组（流式）
            // 【关键修复】使用更精确的方法，只提取keyFindings数组内的字符串
            const findingsStartMatch = summaryJsonText.match(/"keyFindings"\s*:\s*\[/);
            if (findingsStartMatch) {
                const findingsStart = findingsStartMatch.index + findingsStartMatch[0].length;
                // 找到keyFindings数组的结束位置（遇到 ] 且不在引号内）
                let findingsEnd = -1;
                let inString = false;
                let escapeNext = false;
                
                for (let i = findingsStart; i < summaryJsonText.length; i++) {
                    const char = summaryJsonText[i];
                    
                    if (escapeNext) {
                        escapeNext = false;
                        continue;
                    }
                    
                    if (char === '\\') {
                        escapeNext = true;
                        continue;
                    }
                    
                    if (char === '"') {
                        inString = !inString;
                    }
                    
                    if (!inString && char === ']') {
                        findingsEnd = i;
                        break;
                    }
                }
                
                const findingsContent = findingsEnd > findingsStart 
                    ? summaryJsonText.substring(findingsStart, findingsEnd)
                    : summaryJsonText.substring(findingsStart);
                
                console.log('[parseStreamingJSON] keyFindings数组内容长度:', findingsContent.length);
                
                // 尝试完整解析
                try {
                    const findingsArray = JSON.parse('[' + findingsContent + ']');
                    partialResult.keyFindings = findingsArray;
                    console.log('[parseStreamingJSON] ✓ 流式解析keyFindings (完整):', findingsArray.length, '条');
                } catch (e2) {
                    // JSON不完整，尝试逐个提取
                    console.log('[parseStreamingJSON] keyFindings数组不完整，尝试流式逐个提取...');
                    const findings = [];
                    const findingRegex = /"((?:[^"\\]|\\.)*)"/g;
                    let findingMatch;
                    
                    while ((findingMatch = findingRegex.exec(findingsContent)) !== null) {
                        const finding = findingMatch[1]
                            .replace(/\\"/g, '"')
                            .replace(/\\n/g, '\n')
                            .replace(/\\t/g, '\t')
                            .replace(/\\\\/g, '\\');
                        
                        if (finding.trim()) {
                            findings.push(finding);
                        }
                    }
                    
                    if (findings.length > 0) {
                        partialResult.keyFindings = findings;
                        console.log('[parseStreamingJSON] ✓ 流式逐个提取keyFindings:', findings.length, '条');
                    }
                }
            }
            
            // 提取summary字段（流式）
            // 【关键修复】使用更精确的方法提取summary字段的值
            const summaryStartMatch = summaryJsonText.match(/"summary"\s*:\s*"/);
            if (summaryStartMatch) {
                const summaryStart = summaryStartMatch.index + summaryStartMatch[0].length;
                // 从summary字段的开始引号后，找到结束引号（处理转义）
                let summaryEnd = -1;
                let escapeNext = false;
                
                for (let i = summaryStart; i < summaryJsonText.length; i++) {
                    const char = summaryJsonText[i];
                    
                    if (escapeNext) {
                        escapeNext = false;
                        continue;
                    }
                    
                    if (char === '\\') {
                        escapeNext = true;
                        continue;
                    }
                    
                    if (char === '"') {
                        summaryEnd = i;
                        break;
                    }
                }
                
                // 提取summary内容（即使未找到结束引号也提取）
                const summaryContent = summaryEnd > summaryStart
                    ? summaryJsonText.substring(summaryStart, summaryEnd)
                    : summaryJsonText.substring(summaryStart);
                
                const summaryText = summaryContent
                    .replace(/\\"/g, '"')
                    .replace(/\\n/g, '\n')
                    .replace(/\\t/g, '\t')
                    .replace(/\\\\/g, '\\');
                
                if (summaryText.trim()) {
                    partialResult.summary = summaryText;
                    console.log('[parseStreamingJSON] ✓ 流式解析summary, 长度:', summaryText.length);
                }
            }
            
            // 如果有任何部分解析成功，就设置结果
            if (Object.keys(partialResult).length > 0) {
                result.summaryPhase = partialResult;
                console.log('[parseStreamingJSON] ✓ 流式部分解析成功，包含字段:', Object.keys(partialResult));
            }
        }
    }
    
    // 保留原有的CHART_DATA解析（兼容性）
    console.log('[parseStreamingJSON] 开始查找CHART_DATA标签（完整模式）...');
    const chartDataStartPos = content.indexOf('<CHART_DATA>');
    if (chartDataStartPos !== -1) {
        const chartDataContentStart = chartDataStartPos + 12; // '<CHART_DATA>'.length
        const chartDataEndPos = content.indexOf('</CHART_DATA>', chartDataContentStart);
        
        if (chartDataEndPos !== -1) {
            console.log('[parseStreamingJSON] ✓ 找到完整的CHART_DATA标签');
            const chartDataContent = content.substring(chartDataContentStart, chartDataEndPos).trim();
            console.log('[parseStreamingJSON] CHART_DATA内容长度:', chartDataContent.length);
            
            try {
                const parsed = safeJSONParse(chartDataContent);
                if (parsed) {
                    result.summaryPhase = parsed;
                    console.log('[parseStreamingJSON] ✓✓✓ 成功解析CHART_DATA完整JSON');
                } else {
                    console.error('[parseStreamingJSON] ✗ CHART_DATA JSON解析失败（返回null）');
                }
            } catch (e) {
                console.error('[parseStreamingJSON] ✗ CHART_DATA JSON解析失败:', e.message);
            }
        }
    }
    
    console.log('[parseStreamingJSON] ========== 解析完成 ==========');
    console.log('[parseStreamingJSON] 第一阶段结果:', result.firstPhase ? '有数据' : '无数据');
    console.log('[parseStreamingJSON] 第二阶段结果:', result.summaryPhase ? '有数据' : '无数据');
    
    return result;
}

/**
 * 从文本中提取可能的JSON对象（保留以备用）
 * @param {string} text - 文本内容
 * @returns {Array<string>} JSON字符串数组
 */
function extractJSONObjects(text) {
    const candidates = [];
    let braceCount = 0;
    let startIndex = -1;
    
    for (let i = 0; i < text.length; i++) {
        const char = text[i];
        
        if (char === '{') {
            if (braceCount === 0) {
                startIndex = i;
            }
            braceCount++;
        } else if (char === '}') {
            braceCount--;
            if (braceCount === 0 && startIndex >= 0) {
                // 找到一个完整的{}对
                const candidate = text.substring(startIndex, i + 1);
                candidates.push(candidate);
                startIndex = -1;
            }
        }
    }
    
    return candidates;
}

/**
 * 显示第一阶段：分析计划
 * @param {HTMLElement} messageEl - 消息元素
 * @param {Object} firstPhase - 第一阶段JSON
 */
function displayFirstPhase(messageEl, firstPhase) {
    if (!firstPhase) return;
    
    const titleContainer = messageEl.querySelector('.analysis-title-container');
    const tasksContainer = messageEl.querySelector('.analysis-tasks-container');
    
    // 1. 显示标题
    if (firstPhase.title && titleContainer) {
        let titleTextEl = titleContainer.querySelector('.analysis-title-text');
        if (!titleTextEl) {
            titleContainer.innerHTML = `<div class="analysis-title"><h3>📊 <span class="analysis-title-text"></span></h3></div>`;
            titleTextEl = titleContainer.querySelector('.analysis-title-text');
        }
        
        const decodedTitle = decodeUnicodeString(firstPhase.title);
        const escapedTitle = escapeHTML(decodedTitle);
        
        if (titleTextEl.textContent !== decodedTitle) {
            titleTextEl.innerHTML = escapedTitle;
            console.log('[第一阶段] 标题已显示:', decodedTitle);
        }
    }
    
    // 2. 显示任务列表
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
        }
        
        const currentTaskCount = taskListEl.querySelectorAll('.task-item').length;
        
        // 只添加新任务
        for (let i = currentTaskCount; i < firstPhase.tasks.length; i++) {
            const task = firstPhase.tasks[i];
            const taskId = task.id;
            const taskDesc = decodeUnicodeString(task.description);
            const escapedTaskDesc = escapeHTML(taskDesc);
            
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
            console.log(`[第一阶段] 任务${taskId}已创建:`, taskDesc);
            
            // 绑定点击事件
            bindTaskClickEvent(taskListEl, taskId);
        }
    }
    
    // 3. 显示分析详情（展开任务）
    if (firstPhase.analysis && Array.isArray(firstPhase.analysis) && tasksContainer) {
        firstPhase.analysis.forEach(item => {
            const taskId = item.taskId;
            const analysisContent = decodeUnicodeString(item.content);
            
            const taskItem = tasksContainer.querySelector(`[data-task-id="${taskId}"]`);
            if (taskItem) {
                let detailDiv = taskItem.querySelector('.task-detail');
                if (detailDiv) {
                    const escapedContent = escapeHTML(analysisContent).replace(/\n/g, '<br>');
                    
                    if (detailDiv.innerHTML !== escapedContent) {
                        detailDiv.innerHTML = escapedContent;
                        detailDiv.style.display = 'block'; // 自动展开
                        
                        const expandIcon = taskItem.querySelector('.expand-icon');
                        if (expandIcon) expandIcon.textContent = '▲';
                        taskItem.classList.add('expanded');
                        
                        // 更新任务状态为processing
                        const taskIcon = taskItem.querySelector('.task-icon');
                        if (taskIcon) taskIcon.textContent = '🔄';
                        
                        console.log(`[第一阶段] 任务${taskId}分析详情已显示`);
                    }
                }
            }
        });
    }
    
    // 4. 显示SQL执行提示
    if (firstPhase.sql) {
        const summaryContainer = messageEl.querySelector('.analysis-summary-container');
        if (summaryContainer && !summaryContainer.querySelector('.processing-indicator')) {
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
            console.log('[第一阶段] SQL执行提示已显示');
        }
    }
}

/**
 * 显示第二阶段：总结和关键发现
 * @param {HTMLElement} messageEl - 消息元素
 * @param {Object} summaryPhase - 第二阶段JSON
 */
function displaySummaryPhase(messageEl, summaryPhase) {
    if (!summaryPhase) return;
    
    const tasksContainer = messageEl.querySelector('.analysis-tasks-container');
    const findingsContainer = messageEl.querySelector('.analysis-findings-container');
    const summaryContainer = messageEl.querySelector('.analysis-summary-container');
    
    // 1. 更新任务状态
    if (summaryPhase.taskStatus && Array.isArray(summaryPhase.taskStatus) && tasksContainer) {
        summaryPhase.taskStatus.forEach(task => {
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
    
    // 2. 显示关键发现
    if (summaryPhase.keyFindings && Array.isArray(summaryPhase.keyFindings) && findingsContainer) {
        if (!findingsContainer.querySelector('.findings-container')) {
            findingsContainer.innerHTML = `
                <div class="findings-container">
                    <h4>💡 关键发现：</h4>
                    <ul class="findings-list"></ul>
                </div>
            `;
        }
        
        const findingsList = findingsContainer.querySelector('.findings-list');
        findingsList.innerHTML = ''; // 清空旧内容
        
        summaryPhase.keyFindings.forEach((finding, index) => {
            const decodedFinding = decodeUnicodeString(finding);
            const escapedFinding = escapeHTML(decodedFinding);
            
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
            }, 50 + index * 100);
        });
        
        console.log(`[第二阶段] ${summaryPhase.keyFindings.length}条关键发现已显示`);
    }
    
    // 3. 显示总结
    if (summaryPhase.summary && summaryContainer) {
        // 移除处理中提示
        const processingEl = summaryContainer.querySelector('.processing-indicator');
        if (processingEl) processingEl.remove();
        
        const decodedSummary = decodeUnicodeString(summaryPhase.summary);
        const escapedSummary = escapeHTML(decodedSummary).replace(/\n/g, '<br>');
        
        summaryContainer.innerHTML = `
            <div class="summary-container">
                <h4>📝 分析总结：</h4>
                <p class="summary-text">${escapedSummary}</p>
            </div>
        `;
        
        console.log('[第二阶段] 分析总结已显示');
    }
}

/**
 * 绑定任务点击事件
 * @param {HTMLElement} taskListEl - 任务列表元素
 * @param {number} taskId - 任务ID
 */
function bindTaskClickEvent(taskListEl, taskId) {
    const taskItem = taskListEl.querySelector(`[data-task-id="${taskId}"]`);
    const taskHeader = taskItem?.querySelector('.task-header');
    
    if (taskHeader && taskItem) {
        taskHeader.style.cursor = 'pointer';
        taskHeader.addEventListener('click', (e) => {
            e.stopPropagation();
            const detailDiv = taskItem.querySelector('.task-detail');
            const expandIcon = taskItem.querySelector('.expand-icon');
            if (detailDiv && expandIcon) {
                const isExpanded = detailDiv.style.display !== 'none';
                detailDiv.style.display = isExpanded ? 'none' : 'block';
                expandIcon.textContent = isExpanded ? '▼' : '▲';
                taskItem.classList.toggle('expanded', !isExpanded);
            }
        });
    }
}

/**
 * HTML转义
 * @param {string} text - 原始文本
 * @returns {string} 转义后的文本
 */
function escapeHTML(text) {
    if (!text) return '';
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

// 导出函数供chat.js使用
window.parseStreamingJSON = parseStreamingJSON;
window.displayFirstPhase = displayFirstPhase;
window.displaySummaryPhase = displaySummaryPhase;

console.log('chat-json-parser.js 已加载');

