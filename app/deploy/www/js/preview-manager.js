// 预览修改管理器 - 处理临时表数据预览功能
const PreviewManager = {
    // 状态管理
    isPreviewMode: false,
    isExcelPreviewMode: false,
    hasModifications: false,
    modifiedTables: new Set(),
    currentConnectionId: null,

    // 拖拽相关
    draggedElement: null,
    dragOffset: { x: 0, y: 0 },

    // 初始化
    init() {
        console.log('[PreviewManager] ======= 初始化预览管理器 =======');
        console.log('[PreviewManager] 当前CONFIG:', CONFIG);
        console.log('[PreviewManager] 当前APP_STATE:', APP_STATE);
        console.log('[PreviewManager] DOM内容加载状态:', document.readyState);

        // 恢复按钮位置
        this.restoreButtonPositions();

        this.bindEvents();
        console.log('[PreviewManager] 事件绑定完成');

        // 初始化时设置正确的currentConnectionId（但不立即检查修改状态，避免干扰聊天功能）
        const currentPage = this.getCurrentPage();
        if (currentPage === 'database' && APP_STATE.databases?.current?.connectionId) {
            this.currentConnectionId = APP_STATE.databases.current.connectionId;
            console.log('[PreviewManager] 初始化时设置数据库connectionId:', this.currentConnectionId);
        } else if (currentPage === 'excel') {
            this.currentConnectionId = 'excel_default';
            console.log('[PreviewManager] 初始化时设置Excel默认connectionId:', this.currentConnectionId);
        }

        // 只更新按钮可见性，不立即检查修改状态（避免初始化时发送请求干扰聊天功能）
        this.updateButtonVisibility();
        
        console.log('[PreviewManager] 初始化完成 =======');

        // 验证关键方法是否存在
        console.log('[PreviewManager] 方法检查:', {
            toggleExcelPreviewMode: typeof this.toggleExcelPreviewMode === 'function',
            togglePreviewMode: typeof this.togglePreviewMode === 'function',
            hasExcelFile: typeof this.hasExcelFile === 'function'
        });
    },

    // 绑定事件
    bindEvents() {
        console.log('[PreviewManager] 绑定事件监听器');

        // 监听数据库连接变化
        document.addEventListener('databaseConnected', (event) => {
            console.log('[PreviewManager] 收到databaseConnected事件:', event.detail);
            this.currentConnectionId = event.detail.connectionId;
            console.log('[PreviewManager] 设置currentConnectionId:', this.currentConnectionId);
            this.updateButtonVisibility();
            this.checkModificationStatus();
        });

        // 监听文件加载事件（用于Excel文件）
        document.addEventListener('excelFileLoaded', (event) => {
            console.log('[PreviewManager] 收到excelFileLoaded事件');
            // Excel文件加载时，设置默认连接并检查修改状态
            const currentPage = this.getCurrentPage();
            if (currentPage === 'excel') {
                this.currentConnectionId = 'excel_default';
                console.log('[PreviewManager] Excel文件加载，设置默认连接ID');

                // 立即检查修改状态
                setTimeout(() => {
                    this.checkModificationStatus();
                }, 500); // 给后端一些处理时间
            }
        });

        // 监听AI消息完成（通过监听消息完成事件）
        document.addEventListener('messageCompleted', (event) => {
            console.log('[PreviewManager] 收到messageCompleted事件');
            const { content, messageId } = event.detail;
            this.onMessageCompleted(content, messageId);
        });

        // 监听页面切换
        document.addEventListener('pageChanged', (event) => {
            console.log('[PreviewManager] 收到pageChanged事件:', event.detail);

            // 页面切换时更新connectionId
            const page = event.detail.page;
            if (page === 'database') {
                this.currentConnectionId = APP_STATE.databases?.current?.connectionId;
                console.log('[PreviewManager] 切换到数据库页面，connectionId:', this.currentConnectionId);
            } else if (page === 'excel') {
                // Excel页面使用默认连接
                this.currentConnectionId = 'excel_default';
                console.log('[PreviewManager] 切换到Excel页面，使用默认连接');
            }

            // 页面切换时更新按钮显示状态
            setTimeout(() => {
                this.updateButtonVisibility();
            }, 100); // 延迟执行，确保页面切换完成
        });

        // 监听Excel文件上传完成
        document.addEventListener('excelFileLoaded', (event) => {
            console.log('[PreviewManager] 收到excelFileLoaded事件');
            // Excel文件加载完成后，检查是否需要显示按钮
            setTimeout(() => {
                this.updateButtonVisibility();
            }, 200);
        });

        // 拖拽事件监听
        this.bindDragEvents();

        console.log('[PreviewManager] 事件监听器绑定完成');
    },

    // 检查修改状态
    async checkModificationStatus() {
        console.log('[PreviewManager] checkModificationStatus被调用');

        if (!this.currentConnectionId) {
            this.currentConnectionId = APP_STATE.databases?.current?.connectionId;
            console.log('[PreviewManager] 从APP_STATE获取connectionId:', this.currentConnectionId);
        }

        // 如果还是没有connectionId，尝试使用默认的Excel连接
        if (!this.currentConnectionId) {
            const currentPage = this.getCurrentPage();
            if (currentPage === 'excel') {
                this.currentConnectionId = 'excel_default';
                console.log('[PreviewManager] 使用默认Excel连接ID:', this.currentConnectionId);
            }
        }

        if (!this.currentConnectionId) {
            console.log('[PreviewManager] 没有connectionId，设置无修改');
            this.setHasModifications(false);
            return;
        }

        try {
            console.log('[PreviewManager] 开始检查修改状态，connectionId:', this.currentConnectionId);

            const response = await this.callGetConnectionStatus(this.currentConnectionId);
            console.log('[PreviewManager] API响应:', response);

            if (response.success) {
                const hasMods = response.result?.hasModifications || false;
                const tempTables = response.result?.tempTables || [];

                this.modifiedTables = new Set(tempTables);
                this.setHasModifications(hasMods);

                console.log('[PreviewManager] 修改状态检查完成:', { hasMods, tempTables });
            } else {
                console.warn('[PreviewManager] 检查修改状态失败:', response.message);
                this.setHasModifications(false);
            }
        } catch (error) {
            console.error('[PreviewManager] 检查修改状态异常:', error);
            this.setHasModifications(false);
        }
    },

    // 调用后端GetConnectionStatus接口
    async callGetConnectionStatus(connectionId) {
        console.log('[PreviewManager] 准备调用GetConnectionStatus API');

        const requestId = generateRequestId();
        const sessionId = SessionManager.getSessionId() || '';
        const url = `${CONFIG.API_BASE_URL}/api/db/connection/status`;
        const body = {
            requestId,
            sessionId,
            dbConnectId: connectionId
        };

        console.log('[PreviewManager] API请求URL:', url);
        console.log('[PreviewManager] 请求参数:', { requestId, sessionId: sessionId || 'EMPTY', connectionId });
        console.log('[PreviewManager] SessionManager状态:', {
            hasSessionManager: typeof SessionManager !== 'undefined',
            getSessionIdMethod: typeof SessionManager?.getSessionId,
            sessionIdValue: SessionManager?.getSessionId?.()
        });

        try {
            console.log('[PreviewManager] 发起网络请求...');
            console.time('[PreviewManager] GetConnectionStatus请求耗时');

            const result = await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(body)
            });

            console.timeEnd('[PreviewManager] GetConnectionStatus请求耗时');
            console.log('[PreviewManager] API响应结果:', result);
            console.log('[PreviewManager] API响应状态:', result.success ? '成功' : '失败');

            return result;
        } catch (error) {
            console.error('[PreviewManager] API请求异常:', error);
            console.error('[PreviewManager] 异常详情:', {
                message: error.message,
                stack: error.stack,
                name: error.name
            });
            throw error;
        }
    },

    // 设置是否有修改
    setHasModifications(hasMods) {
        this.hasModifications = hasMods;
        this.updateButtonVisibility();
    },

    // 更新按钮显示状态
    updateButtonVisibility() {
        const currentPage = this.getCurrentPage();
        const hasExcelFile = this.hasExcelFile();

        console.log('[PreviewManager] 更新按钮显示:', {
            currentPage,
            hasModifications: this.hasModifications,
            hasExcelFile,
            isPreviewMode: this.isPreviewMode,
            isExcelPreviewMode: this.isExcelPreviewMode
        });

        // 数据库页面按钮：只要在数据库页面且有连接就显示
        const dbBtn = document.getElementById('preview-float-button');
        if (dbBtn) {
            const shouldShowDb = currentPage === 'database' && this.currentConnectionId;
            console.log('[PreviewManager] 数据库按钮:', shouldShowDb ? '显示' : '隐藏');

            if (shouldShowDb) {
                dbBtn.classList.remove('hidden');
            } else {
                dbBtn.classList.add('hidden');
                if (this.isPreviewMode) {
                    this.exitPreviewMode();
                }
            }
        }

        // Excel页面按钮
        const excelBtn = document.getElementById('excel-preview-float-button');
        if (excelBtn) {
            // Excel页面只要有Excel文件就显示按钮（修改状态通过API检查）
            const shouldShowExcel = currentPage === 'excel' && hasExcelFile;
            console.log('[PreviewManager] Excel按钮:', shouldShowExcel ? '显示' : '隐藏', { currentPage, hasExcelFile });

            if (shouldShowExcel) {
                excelBtn.classList.remove('hidden');
            } else {
                excelBtn.classList.add('hidden');
                if (this.isExcelPreviewMode) {
                    this.exitExcelPreviewMode();
                }
            }
        }
    },

    // 获取当前页面
    getCurrentPage() {
        const chatPage = document.getElementById('chat-page');
        const dbPage = document.getElementById('db-chat-page');

        if (chatPage && chatPage.classList.contains('active')) {
            return 'excel';
        } else if (dbPage && dbPage.classList.contains('active')) {
            return 'database';
        }
        return 'unknown';
    },

    // 检查是否在Excel页面
    isExcelPageActive() {
        const chatPage = document.getElementById('chat-page');
        return chatPage && chatPage.classList.contains('active');
    },

    // 检查是否有Excel文件正在预览
    hasExcelFile() {
        const excelPreview = document.getElementById('excel-preview');
        const uploadPrompt = document.getElementById('upload-prompt');

        // Excel预览区域可见且上传提示隐藏时，说明有Excel文件
        const hasPreview = excelPreview && (excelPreview.style.display === 'flex' || excelPreview.style.display === 'block');
        const uploadHidden = uploadPrompt && uploadPrompt.style.display === 'none';

        console.log('[PreviewManager] Excel文件检测:', { hasPreview, uploadHidden });

        return hasPreview && uploadHidden;
    },

    // 切换预览模式
    async togglePreviewMode() {
        if (this.isPreviewMode) {
            await this.exitPreviewMode();
        } else {
            await this.enterPreviewMode();
        }
    },

    // 进入预览模式
    async enterPreviewMode() {
        console.log('[PreviewManager] 进入预览模式');

        this.isPreviewMode = true;
        this.updateButtonText();

        // 重新加载当前表的数据（自动从临时表获取）
        await this.refreshCurrentTableData();

        // 添加预览模式样式
        this.addPreviewModeStyles();

        if (typeof showMessage === 'function') {
            showMessage('已进入预览模式，显示修改后的数据', 'info');
        }
    },

    // 退出预览模式
    async exitPreviewMode() {
        console.log('[PreviewManager] 退出预览模式');

        this.isPreviewMode = false;
        this.updateButtonText();

        // 强制从原表重新加载数据
        await this.refreshOriginalTableData();

        // 移除预览模式样式
        this.removePreviewModeStyles();

        if (typeof showMessage === 'function') {
            showMessage('已退出预览模式，显示原始数据', 'info');
        }
    },

    // 更新按钮文本
    updateButtonText() {
        // 更新数据库页面按钮
        const dbBtn = document.querySelector('#preview-float-button .preview-btn');
        if (dbBtn) {
            const dbIcon = dbBtn.querySelector('.btn-icon');
            const dbText = dbBtn.querySelector('.btn-text');

            if (this.isPreviewMode) {
                dbBtn.classList.add('preview-mode');
                if (dbIcon) dbIcon.textContent = '🔄';
                if (dbText) dbText.textContent = '查看原数据';
            } else {
                dbBtn.classList.remove('preview-mode');
                if (dbIcon) dbIcon.textContent = '👁️';
                if (dbText) dbText.textContent = '预览修改';
            }
        }

        // 更新Excel页面按钮
        const excelBtn = document.querySelector('#excel-preview-float-button .preview-btn');
        if (excelBtn) {
            const excelIcon = excelBtn.querySelector('.btn-icon');
            const excelText = excelBtn.querySelector('.btn-text');

            if (this.isExcelPreviewMode) {
                excelBtn.classList.add('preview-mode');
                if (excelIcon) excelIcon.textContent = '🔄';
                if (excelText) excelText.textContent = '查看原数据';
            } else {
                excelBtn.classList.remove('preview-mode');
                if (excelIcon) excelIcon.textContent = '👁️';
                if (excelText) excelText.textContent = '预览修改';
            }
        }
    },

    // 刷新当前表数据（从临时表获取）
    async refreshCurrentTableData() {
        const currentTable = this.getCurrentViewedTable();
        if (!currentTable) {
            console.warn('[PreviewManager] 未找到当前查看的表');
            return;
        }

        console.log('[PreviewManager] 刷新表数据（预览模式）:', currentTable);

        try {
            const requestId = generateRequestId();
            const sessionId = SessionManager.getSessionId() || '';
            const url = `${CONFIG.API_BASE_URL}/api/db/table/data`;
            const body = {
                requestId,
                sessionId,
                dbConnectId: this.currentConnectionId,
                tableName: currentTable,
                forceOriginal: false,
                pageNumber: 1,
                pageSize: 50
            };
            
            console.log('[PreviewManager] 刷新表数据请求体:', JSON.stringify(body, null, 2));

            const response = await apiRequest(url, {
                method: 'POST',
                body: JSON.stringify(body)
            });

            console.log('[PreviewManager] 刷新表数据响应:', response);

            if (response.success) {
                // 更新表格显示
                this.updateTableDisplay(response.result, currentTable);
            } else {
                console.error('[PreviewManager] 刷新表数据失败:', response.message);
                if (typeof showMessage === 'function') {
                    showMessage('刷新表数据失败: ' + response.message, 'error');
                }
            }
        } catch (error) {
            console.error('[PreviewManager] 刷新表数据异常:', error);
            if (typeof showMessage === 'function') {
                showMessage('刷新表数据异常', 'error');
            }
        }
    },

    // 刷新原始表数据
    async refreshOriginalTableData() {
        const currentTable = this.getCurrentViewedTable();
        if (!currentTable) {
            console.warn('[PreviewManager] 未找到当前查看的表');
            return;
        }

        console.log('[PreviewManager] 刷新原始表数据:', currentTable);

        try {
            // 强制从原表查询（使用forceOriginal=true参数）
            const response = await this.callGetTableDataWithForceOriginal(this.currentConnectionId, currentTable);

            if (response.success) {
                // 更新表格显示
                this.updateTableDisplay(response.result, currentTable);
            } else {
                console.error('[PreviewManager] 刷新原始表数据失败:', response.message);
                if (typeof showMessage === 'function') {
                    showMessage('刷新原始表数据失败: ' + response.message, 'error');
                }
            }
        } catch (error) {
            console.error('[PreviewManager] 刷新原始表数据异常:', error);
            if (typeof showMessage === 'function') {
                showMessage('刷新原始表数据异常', 'error');
            }
        }
    },

    // 强制从原表获取数据
    async callGetTableDataWithForceOriginal(connectionId, tableName) {
        const requestId = generateRequestId();
        const sessionId = SessionManager.getSessionId() || '';

        const url = `${CONFIG.API_BASE_URL}/api/db/table/data`;
        const body = {
            requestId,
            sessionId,
            dbConnectId: connectionId,
            tableName,
            forceOriginal: true
        };

        return await apiRequest(url, {
            method: 'POST',
            body: JSON.stringify(body)
        });
    },

    // 更新表格显示
    updateTableDisplay(apiResult, tableName) {
        console.log('[PreviewManager] updateTableDisplay 被调用:', { apiResult, tableName });

        if (!apiResult) {
            console.warn('[PreviewManager] updateTableDisplay: API结果为空');
            return;
        }

        try {
            // 转换后端返回的数据格式为前端期望的格式
            let tableSchema = null;

            // 检查API结果格式 - 后端返回的是 response.result = {tableSchema: {...}}
            if (apiResult.tableSchema) {
                tableSchema = {
                    tableName: tableName || this.getCurrentViewedTable() || '表', // 使用传入的表名或从当前状态获取
                    columnInfo: apiResult.tableSchema.columnInfo || [],
                    tableData: apiResult.tableSchema.tableData || []
                };
            }
            // 检查是否已经是前端期望的格式
            else if (apiResult.tableName || apiResult.columnInfo || apiResult.tableData) {
                tableSchema = {
                    tableName: tableName || apiResult.tableName || this.getCurrentViewedTable() || '表',
                    columnInfo: apiResult.columnInfo || [],
                    tableData: apiResult.tableData || []
                };
            }

            if (!tableSchema) {
                console.warn('[PreviewManager] updateTableDisplay: 无法解析表数据格式');
                return;
            }

            console.log('[PreviewManager] 转换后的表数据:', {
                tableName: tableSchema.tableName,
                columnCount: tableSchema.columnInfo ? tableSchema.columnInfo.length : 0,
                rowCount: tableSchema.tableData ? tableSchema.tableData.length : 0
            });

            // 调用全局的 displayTablePreview 函数
            if (typeof displayTablePreview === 'function') {
                displayTablePreview(tableSchema);
                console.log('[PreviewManager] 表格显示已更新');
            } else {
                console.error('[PreviewManager] displayTablePreview 函数未找到');
            }
        } catch (error) {
            console.error('[PreviewManager] updateTableDisplay 异常:', error);
        }
    },

    // 获取当前查看的表名
    getCurrentViewedTable() {
        // 从APP_STATE或当前UI状态获取
        return APP_STATE.databases?.current?.currentTable ||
               document.getElementById('db-table-select')?.value;
    },

    // 添加预览模式样式
    addPreviewModeStyles() {
        const previewContainer = document.getElementById('table-preview-content');
        if (previewContainer) {
            previewContainer.classList.add('table-preview-mode');
        }
    },

    // 移除预览模式样式
    removePreviewModeStyles() {
        const previewContainer = document.getElementById('table-preview-content');
        if (previewContainer) {
            previewContainer.classList.remove('table-preview-mode');
        }
    },

    // 处理AI消息完成
    onMessageCompleted(content, messageId) {
        console.log('[PreviewManager] 收到messageCompleted事件，消息ID:', messageId);
        console.log('[PreviewManager] 消息内容长度:', content.length);

        // 检查消息内容是否包含SQL执行信息
        // 这里需要解析AI返回的内容，看是否有SQL执行相关的标记

        // 检查是否包含SQL执行相关的标记或内容
        const hasSQLExecution = this.detectSQLExecution(content);
        console.log('[PreviewManager] 检测SQL执行结果:', hasSQLExecution);

        if (hasSQLExecution) {
            console.log('[PreviewManager] 检测到SQL执行，延迟检查修改状态');

            // 延迟检查修改状态，给后端处理时间
            setTimeout(() => {
                this.checkModificationStatus();
            }, 1500); // 稍微增加延迟时间
        }
    },

    // 检测消息内容是否包含SQL执行
    detectSQLExecution(content) {
        // 检查是否包含SQL相关的标记或内容
        // 这里可以根据实际的AI响应格式来调整检测逻辑

        // 方法1：检查是否包含SQL关键词
        const sqlKeywords = ['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'CREATE', 'DROP', 'ALTER'];
        const upperContent = content.toUpperCase();
        const hasSQL = sqlKeywords.some(keyword => upperContent.includes(keyword));

        // 方法2：检查是否包含执行成功的标记
        const hasExecutionMark = content.includes('执行成功') ||
                                content.includes('执行完成') ||
                                content.includes('SQL执行');

        // 方法3：检查是否包含数据库操作相关的响应格式
        const hasDatabaseResponse = content.includes('<CHART_DATA>') ||
                                   content.includes('数据库操作');

        return hasSQL || hasExecutionMark || hasDatabaseResponse;
    },

    // 处理SQL执行完成（保留接口以备将来使用）
    onSQLExecuted(sql, result) {
        // 检查是否是非查询SQL（修改类）
        if (!result.is_query && result.error_code === 0) {
            console.log('[PreviewManager] 检测到修改类SQL执行，刷新状态');

            // 延迟检查修改状态，给后端处理时间
            setTimeout(() => {
                this.checkModificationStatus();
            }, 1000);
        }
    },

    // Excel预览模式切换
    async toggleExcelPreviewMode() {
        console.log('[PreviewManager] toggleExcelPreviewMode 被调用, 当前模式:', this.isExcelPreviewMode);

        try {
            if (this.isExcelPreviewMode) {
                await this.exitExcelPreviewMode();
            } else {
                await this.enterExcelPreviewMode();
            }
            console.log('[PreviewManager] toggleExcelPreviewMode 执行完成');
        } catch (error) {
            console.error('[PreviewManager] toggleExcelPreviewMode 执行失败:', error);
            throw error;
        }
    },

    // 进入Excel预览模式
    async enterExcelPreviewMode() {
        console.log('[PreviewManager] 进入Excel预览模式');

        this.isExcelPreviewMode = true;
        this.updateButtonText();

        // Excel预览逻辑：尝试从临时表重新加载数据
        await this.refreshExcelPreviewData();

        // 添加预览模式样式
        this.addExcelPreviewModeStyles();

        if (typeof showMessage === 'function') {
            showMessage('已进入Excel预览模式，显示修改后的数据', 'info');
        }
    },

    // 退出Excel预览模式
    async exitExcelPreviewMode() {
        console.log('[PreviewManager] 退出Excel预览模式');

        this.isExcelPreviewMode = false;
        this.updateButtonText();

        // 移除预览模式样式
        this.removeExcelPreviewModeStyles();

        // 重新加载原表数据
        await this.reloadExcelDataFromOriginalTables();

        if (typeof showMessage === 'function') {
            showMessage('已退出Excel预览模式', 'info');
        }
    },

    // 从原表重新加载Excel数据
    async reloadExcelDataFromOriginalTables() {
        console.log('[PreviewManager] reloadExcelDataFromOriginalTables 被调用');

        try {
            const excelFileId = APP_STATE.currentFile?.id;
            if (!excelFileId) {
                console.error('[PreviewManager] 没有找到Excel文件ID');
                return { success: false, message: '没有找到Excel文件ID' };
            }

            console.log('[PreviewManager] Excel文件ID:', excelFileId);

            // 首先获取Excel文件的worksheet信息
            const previewResponse = await FileAPI.previewExcel(excelFileId, 1, 50);
            if (!previewResponse.success || !previewResponse.result) {
                console.error('[PreviewManager] 获取Excel文件信息失败');
                return { success: false, message: '获取Excel文件信息失败' };
            }

            const excelData = previewResponse.result;
            const worksheets = excelData.excelData?.sheets || [];
            console.log('[PreviewManager] Excel worksheets:', worksheets.map(w => w.name));

            // 对每个worksheet，获取原表数据（forceOriginal=true）
            const connectionId = 'excel_default';
            const originalSheets = [];

            for (const worksheet of worksheets) {
                // 计算表名
                const tableName = this.calculateExcelTableName(worksheet.name, excelFileId);
                console.log('[PreviewManager] 获取原表数据:', tableName);

                // 调用getTableData获取数据，forceOriginal=true
                const tableDataResponse = await getTableDataWithForceOriginal(connectionId, tableName, 1, 50, true);
                
                if (tableDataResponse.success && tableDataResponse.result) {
                    console.log('[PreviewManager] API响应result详情:', JSON.stringify(tableDataResponse.result).substring(0, 500));
                    const schema = tableDataResponse.result.tableSchema;
                    // 正确解析tableData结构：可能是数组，也可能是{rows: [...], currentPage: 1, pageSize: 50}对象
                    let tableData = schema?.tableData;
                    if (tableData && typeof tableData === 'object' && !Array.isArray(tableData)) {
                        // 如果是对象结构，取rows数组
                        tableData = tableData.rows || [];
                    }
                    tableData = tableData || [];
                    const columnInfo = schema?.columnInfo || [];
                    const totalRows = schema?.totalRows || (tableData?.length || 0);
                    
                    originalSheets.push({
                        name: worksheet.name,
                        totalRows: totalRows,
                        colCount: columnInfo.length || worksheet.colCount || 0,
                        currentPage: 1,
                        totalPages: 1,
                        pageSize: 50,
                        columns: columnInfo.map(c => c.name || c).filter(Boolean) || worksheet.columns || [],
                        data: this.convertRowsToSheetData(tableData)
                    });
                    console.log('[PreviewManager] 成功获取原表数据:', tableName, '行数:', tableData.length);
                } else {
                    // 如果获取失败，使用原始worksheet数据
                    originalSheets.push(worksheet);
                    console.warn('[PreviewManager] 获取原表数据失败，使用原始数据:', tableName);
                }
            }

            // 构建新的Excel数据并显示
            const newExcelData = {
                fileId: excelData.fileId,
                fileName: excelData.fileName,
                fileSize: excelData.fileSize,
                excelData: {
                    sheets: originalSheets
                }
            };

            console.log('[PreviewManager] 显示原表数据');
            displayExcelPreview(newExcelData);

            return {
                success: true,
                message: `成功加载原表数据`
            };

        } catch (error) {
            console.error('[PreviewManager] reloadExcelDataFromOriginalTables 异常:', error);
            return { success: false, message: error.message };
        }
    },


    // 添加Excel预览模式样式
    addExcelPreviewModeStyles() {
        const excelPreview = document.getElementById('excel-preview');
        if (excelPreview) {
            excelPreview.classList.add('table-preview-mode');
        }
    },

    // 移除Excel预览模式样式
    removeExcelPreviewModeStyles() {
        const excelPreview = document.getElementById('excel-preview');
        if (excelPreview) {
            excelPreview.classList.remove('table-preview-mode');
        }
    },

    // 刷新Excel预览数据
    async refreshExcelPreviewData() {
        console.log('[PreviewManager] 刷新Excel预览数据');
        console.log('[PreviewManager] 当前connectionId:', this.currentConnectionId);
        console.log('[PreviewManager] Excel文件检测:', this.hasExcelFile());
        console.log('[PreviewManager] 当前页面:', this.getCurrentPage());

        try {
            // 检查是否有Excel文件
            if (!this.hasExcelFile()) {
                console.log('[PreviewManager] 没有Excel文件，跳过刷新');
                return;
            }

            // 对于Excel预览，目前的实现是：
            // 1. 检查是否有临时表数据（通过API调用）
            // 2. 如果有，从临时表重新加载数据
            // 3. 如果没有，显示提示信息

            console.log('[PreviewManager] 开始检查Excel连接状态');

            // 尝试调用GetConnectionStatus来检查是否有Excel相关的修改
            if (this.currentConnectionId) {
                console.log('[PreviewManager] 调用GetConnectionStatus API');
                const response = await this.callGetConnectionStatus(this.currentConnectionId);
                console.log('[PreviewManager] Excel连接状态:', response);

                if (response.success && response.result) {
                    const hasMods = response.result.hasTempTables || false;
                    const tempTables = response.result.tempTables || [];

                    if (hasMods && tempTables.length > 0) {
                        console.log('[PreviewManager] 发现Excel临时表，准备重新加载数据');
                        console.log('[PreviewManager] 临时表列表:', tempTables);

                        // 发送Excel数据重新加载请求
                        try {
                            console.log('[PreviewManager] 发送Excel数据重新加载请求');
                            const reloadResult = await this.reloadExcelDataFromTempTables(tempTables);
                            console.log('[PreviewManager] Excel数据重新加载结果:', reloadResult);

                            if (reloadResult.success) {
                                if (typeof showMessage === 'function') {
                                    showMessage(`成功加载 ${tempTables.length} 个修改后的表格数据`, 'success');
                                }
                            } else {
                                if (typeof showMessage === 'function') {
                                    showMessage('重新加载Excel数据失败: ' + reloadResult.message, 'error');
                                }
                            }
                        } catch (reloadError) {
                            console.error('[PreviewManager] 重新加载Excel数据异常:', reloadError);
                            if (typeof showMessage === 'function') {
                                showMessage('重新加载Excel数据异常', 'error');
                            }
                        }
                    } else {
                        console.log('[PreviewManager] 没有发现Excel修改');
                        if (typeof showMessage === 'function') {
                            showMessage('当前没有修改的数据，显示原始Excel内容', 'info');
                        }
                    }
                }
            } else {
                console.log('[PreviewManager] 没有连接ID，跳过API调用');
                if (typeof showMessage === 'function') {
                    showMessage('Excel预览模式已开启', 'info');
                }
            }

        } catch (error) {
            console.error('[PreviewManager] 刷新Excel预览数据失败:', error);
            if (typeof showMessage === 'function') {
                showMessage('刷新Excel预览数据失败: ' + error.message, 'error');
            }
        }
    },

    // 从临时表重新加载Excel数据
    async reloadExcelDataFromTempTables(tempTables) {
        console.log('[PreviewManager] reloadExcelDataFromTempTables 被调用');
        console.log('[PreviewManager] 临时表列表:', tempTables);

        try {
            // 检查是否有Excel文件ID
            const excelFileId = APP_STATE.currentFile?.id;
            if (!excelFileId) {
                console.error('[PreviewManager] 没有找到Excel文件ID');
                return { success: false, message: '没有找到Excel文件ID' };
            }

            console.log('[PreviewManager] Excel文件ID:', excelFileId);

            // 首先获取Excel文件的worksheet信息
            const previewResponse = await FileAPI.previewExcel(excelFileId, 1, 50);
            if (!previewResponse.success || !previewResponse.result) {
                console.error('[PreviewManager] 获取Excel文件信息失败');
                return { success: false, message: '获取Excel文件信息失败' };
            }

            const excelData = previewResponse.result;
            const worksheets = excelData.excelData?.sheets || [];
            console.log('[PreviewManager] Excel worksheets:', worksheets.map(w => w.name));

            // 对每个worksheet，获取临时表数据
            const connectionId = 'excel_default';
            const modifiedSheets = [];

            for (const worksheet of worksheets) {
                // 计算表名：sanitize(worksheetName) + "_" + fileId
                const tableName = this.calculateExcelTableName(worksheet.name, excelFileId);
                console.log('[PreviewManager] 处理worksheet:', worksheet.name, '-> 表名:', tableName);

                // 检查这个表是否有临时表
                const hasTempTable = tempTables.some(t => t.includes(tableName) || tableName.includes(t.replace('_backup_' + excelFileId, '')));
                
                if (hasTempTable || tempTables.includes(tableName)) {
                    // 有临时表，调用getTableData获取数据（forceOriginal=false会自动获取临时表数据）
                    const tableDataResponse = await getTableData(connectionId, tableName, 1, 50);
                    
                    if (tableDataResponse.success && tableDataResponse.result) {
                        console.log('[PreviewManager] API响应result详情:', JSON.stringify(tableDataResponse.result).substring(0, 500));
                        const schema = tableDataResponse.result.tableSchema;
                        // 正确解析tableData结构：可能是数组，也可能是{rows: [...], currentPage: 1, pageSize: 50}对象
                        let tableData = schema?.tableData;
                        if (tableData && typeof tableData === 'object' && !Array.isArray(tableData)) {
                            // 如果是对象结构，取rows数组
                            tableData = tableData.rows || [];
                        }
                        tableData = tableData || [];
                        const columnInfo = schema?.columnInfo || [];
                        const totalRows = schema?.totalRows || (tableData?.length || 0);
                        
                        modifiedSheets.push({
                            name: worksheet.name,
                            totalRows: totalRows,
                            colCount: columnInfo.length || worksheet.colCount || 0,
                            currentPage: 1,
                            totalPages: 1,
                            pageSize: 50,
                            columns: columnInfo.map(c => c.name || c).filter(Boolean) || worksheet.columns || [],
                            data: this.convertRowsToSheetData(tableData)
                        });
                        console.log('[PreviewManager] 成功获取临时表数据:', tableName, '行数:', tableData.length);
                    } else {
                        // 如果获取失败，使用原始worksheet数据
                        modifiedSheets.push(worksheet);
                        console.warn('[PreviewManager] 获取临时表数据失败，使用原始数据:', tableName);
                    }
                } else {
                    // 没有临时表，使用原始数据
                    modifiedSheets.push(worksheet);
                    console.log('[PreviewManager] 没有临时表，使用原始数据:', tableName);
                }
            }

            // 构建新的Excel数据并显示
            const newExcelData = {
                fileId: excelData.fileId,
                fileName: excelData.fileName,
                fileSize: excelData.fileSize,
                excelData: {
                    sheets: modifiedSheets
                }
            };

            console.log('[PreviewManager] 显示修改后的Excel数据');
            displayExcelPreview(newExcelData);

            return {
                success: true,
                message: `成功加载临时表数据`,
                tempTables: tempTables
            };

        } catch (error) {
            console.error('[PreviewManager] reloadExcelDataFromTempTables 异常:', error);
            return { success: false, message: error.message };
        }
    },

    // 计算Excel表名（与后端calculateTableName一致）
    calculateExcelTableName(worksheetName, fileId) {
        // 将不符合字母、数字、下划线、中文条件的字符替换为_
        let sanitizedName = '';
        for (let i = 0; i < worksheetName.length; i++) {
            const c = worksheetName[i];
            const code = worksheetName.charCodeAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c === '_' || code > 127) {
                sanitizedName += c;
            } else {
                sanitizedName += '_';
            }
        }
        return sanitizedName + '_' + fileId;
    },

    // 将数据库行数据转换为sheet格式的数据
    convertRowsToSheetData(rows) {
        if (!rows || !Array.isArray(rows)) {
            return [];
        }
        return rows.map(row => {
            if (row.cells) {
                return row.cells;
            } else if (Array.isArray(row)) {
                return row;
            } else if (typeof row === 'object') {
                // 如果是对象，转为数组
                return Object.values(row);
            }
            return [row];
        });
    },

    // 绑定拖拽事件
    bindDragEvents() {
        // 为所有预览按钮绑定拖拽事件
        const buttons = document.querySelectorAll('.preview-float-btn[draggable="true"]');

        buttons.forEach(button => {
            // 开始拖拽
            button.addEventListener('dragstart', (e) => {
                this.handleDragStart(e, button);
            });

            // 拖拽结束
            button.addEventListener('dragend', (e) => {
                this.handleDragEnd(e, button);
            });
        });

        // 全局拖拽悬停事件（用于放置）
        document.addEventListener('dragover', (e) => {
            e.preventDefault(); // 允许放置
        });

        document.addEventListener('drop', (e) => {
            e.preventDefault(); // 防止默认行为
        });
    },

    // 处理拖拽开始
    handleDragStart(e, element) {
        console.log('[PreviewManager] 开始拖拽');

        this.draggedElement = element;
        element.classList.add('dragging');

        // 计算鼠标在元素内的偏移量
        const rect = element.getBoundingClientRect();
        this.dragOffset.x = e.clientX - rect.left;
        this.dragOffset.y = e.clientY - rect.top;

        // 设置拖拽数据
        e.dataTransfer.effectAllowed = 'move';
        e.dataTransfer.setData('text/html', element.outerHTML);
    },

    // 处理拖拽结束
    handleDragEnd(e, element) {
        console.log('[PreviewManager] 结束拖拽');

        element.classList.remove('dragging');

        // 计算新位置
        const newX = e.clientX - this.dragOffset.x;
        const newY = e.clientY - this.dragOffset.y;

        // 确保不超出视窗边界
        const maxX = window.innerWidth - element.offsetWidth;
        const maxY = window.innerHeight - element.offsetHeight;

        const clampedX = Math.max(0, Math.min(newX, maxX));
        const clampedY = Math.max(0, Math.min(newY, maxY));

        // 应用新位置
        element.style.left = clampedX + 'px';
        element.style.top = clampedY + 'px';
        element.style.right = 'auto';
        element.style.bottom = 'auto';
        element.style.position = 'fixed';

        this.draggedElement = null;

        // 保存位置到localStorage
        this.saveButtonPosition(element.id, clampedX, clampedY);
    },

    // 保存按钮位置
    saveButtonPosition(buttonId, x, y) {
        const positions = JSON.parse(localStorage.getItem('previewButtonPositions') || '{}');
        positions[buttonId] = { x, y };
        localStorage.setItem('previewButtonPositions', JSON.stringify(positions));
    },

    // 恢复按钮位置
    restoreButtonPositions() {
        const positions = JSON.parse(localStorage.getItem('previewButtonPositions') || '{}');

        Object.keys(positions).forEach(buttonId => {
            const button = document.getElementById(buttonId);
            const pos = positions[buttonId];

            if (button && pos) {
                button.style.left = pos.x + 'px';
                button.style.top = pos.y + 'px';
                button.style.right = 'auto';
                button.style.bottom = 'auto';
                button.style.position = 'fixed';
            }
        });
    }
};

// 全局函数供HTML调用
function togglePreviewMode() {
    console.log('[Global] togglePreviewMode 被调用');
    if (PreviewManager && PreviewManager.togglePreviewMode) {
        PreviewManager.togglePreviewMode().catch(error => {
            console.error('[Global] togglePreviewMode 错误:', error);
        });
    } else {
        console.error('[Global] PreviewManager 没有初始化');
    }
}

function toggleExcelPreviewMode() {
    console.log('[Global] toggleExcelPreviewMode 被调用');
    if (PreviewManager && PreviewManager.toggleExcelPreviewMode) {
        PreviewManager.toggleExcelPreviewMode().catch(error => {
            console.error('[Global] toggleExcelPreviewMode 错误:', error);
        });
    } else {
        console.error('[Global] PreviewManager 没有初始化或没有toggleExcelPreviewMode方法');
    }
}

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', function() {
    console.log('[PreviewManager] DOMContentLoaded事件触发');
    PreviewManager.init();
});

// 如果DOM已经加载完成，立即初始化（防止事件错过）
if (document.readyState === 'loading') {
    console.log('[PreviewManager] DOM仍在加载，等待DOMContentLoaded');
} else {
    console.log('[PreviewManager] DOM已加载，立即初始化');
    PreviewManager.init();
}


// Toast提示功能由utils.js中的showMessage函数提供

// 导出给其他模块使用
window.PreviewManager = PreviewManager;
