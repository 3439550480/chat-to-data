// 文件处理模块

// 预览分页：每页显示行数（与后端一致，后续分页请求按此页大小）
const PREVIEW_PAGE_SIZE = 50;

// Excel分页状态管理
let excelCurrentPage = 1;
let excelTotalPages = 1;
let excelTotalRows = 0;
let currentSheetName = null;

// 显示Excel预览
function displayExcelPreview(excelData) {
    const uploadPrompt = document.getElementById('upload-prompt');
    const excelPreview = document.getElementById('excel-preview');
    const excelFileName = document.getElementById('excel-file-name');
    const sheetTabs = document.getElementById('sheet-tabs');
    const excelTable = document.getElementById('excel-table');
    
    if (!uploadPrompt || !excelPreview || !excelFileName || !sheetTabs || !excelTable) {
        return;
    }
    
    // 隐藏上传提示，显示预览
    uploadPrompt.style.display = 'none';
    excelPreview.style.display = 'flex';
    
    // 更新文件名
    excelFileName.textContent = excelData.fileName || 'Excel文件';
    
    // 更新工作表标签
    sheetTabs.innerHTML = '';
    
    if (excelData.excelData && excelData.excelData.sheets) {
        excelData.excelData.sheets.forEach((sheet, index) => {
            const tab = document.createElement('button');
            tab.className = 'sheet-tab' + (index === 0 ? ' active' : '');
            tab.textContent = sheet.name || `Sheet${index + 1}`;
            tab.addEventListener('click', () => {
                // 切换工作表
                switchSheet(sheet, index);
                
                // 更新标签状态
                document.querySelectorAll('.sheet-tab').forEach(t => {
                    t.classList.remove('active');
                });
                tab.classList.add('active');
            });
            
            sheetTabs.appendChild(tab);
        });
        
        // 显示第一个工作表
        if (excelData.excelData.sheets.length > 0) {
            switchSheet(excelData.excelData.sheets[0], 0);
        }
    }
    
    // 更新文件状态
    if (typeof APP_STATE !== 'undefined' && APP_STATE) {
        APP_STATE.currentFile = {
            id: excelData.fileId,
            name: excelData.fileName,
            size: excelData.fileSize,
            data: excelData
        };
    }

    // 触发Excel文件加载完成事件
    document.dispatchEvent(new CustomEvent('excelFileLoaded', {
        detail: {
            fileId: excelData.fileId,
            fileName: excelData.fileName,
            sheetCount: excelData.excelData?.sheets?.length || 0
        }
    }));
}

// 切换工作表
function switchSheet(sheet, index) {
    const excelTable = document.getElementById('excel-table');
    if (!excelTable) return;
    
    // 更新当前工作表信息
    currentSheetName = sheet.name;
    excelCurrentPage = sheet.currentPage || 1;
    excelTotalPages = sheet.totalPages || 1;
    excelTotalRows = sheet.totalRows || 0;
    
    // 清空表格
    excelTable.innerHTML = '';
    
    if (!sheet.data || sheet.data.length === 0) {
        // 空工作表
        const row = document.createElement('tr');
        const cell = document.createElement('td');
        const colCount = sheet.colCount ?? sheet.columns ?? 1;
        cell.colSpan = Math.max(colCount, 1);
        cell.textContent = '空工作表';
        cell.style.textAlign = 'center';
        cell.style.color = '#6c757d';
        row.appendChild(cell);
        excelTable.appendChild(row);
        return;
    }
    
    // 创建表格结构（类似Excel的布局）
    const headerRow = document.createElement('tr');
    
    // 左上角空白单元格（类似Excel的A1位置）
    const cornerCell = document.createElement('th');
    cornerCell.className = 'corner-cell';
    cornerCell.textContent = '';
    headerRow.appendChild(cornerCell);
    
    // 创建列号表头（A, B, C, ...）
    const colCount = Math.max(sheet.colCount ?? sheet.columns?.length ?? 1, sheet.data[0] ? sheet.data[0].length : 1);
    for (let col = 0; col < colCount; col++) {
        const th = document.createElement('th');
        th.className = 'column-header';
        th.textContent = sheet.columns && sheet.columns[col] ? sheet.columns[col] : getColumnLetter(col);
        headerRow.appendChild(th);
    }
    excelTable.appendChild(headerRow);
    
    // 创建数据行（只显示当前页数据）
    const startRow = (excelCurrentPage - 1) * PREVIEW_PAGE_SIZE;
    for (let rowIndex = 0; rowIndex < sheet.data.length; rowIndex++) {
        const row = document.createElement('tr');
        
        // 添加行号单元格（显示实际行号，不是当前页的行号）
        const rowHeader = document.createElement('th');
        rowHeader.className = 'row-header';
        rowHeader.textContent = (startRow + rowIndex + 1).toString();
        row.appendChild(rowHeader);
        
        // 添加数据单元格
        if (sheet.data[rowIndex]) {
            sheet.data[rowIndex].forEach((cellData, colIndex) => {
                const td = document.createElement('td');
                td.textContent = cellData || '';
                
                // 限制单元格内容长度
                if (td.textContent.length > 100) {
                    td.textContent = td.textContent.substring(0, 100) + '...';
                    td.title = cellData;
                }
                
                row.appendChild(td);
            });
        }
        
        // 如果数据列数不足，补充空单元格
        const currentCols = sheet.data[rowIndex] ? sheet.data[rowIndex].length : 0;
        for (let col = currentCols; col < colCount; col++) {
            const td = document.createElement('td');
            td.textContent = '';
            row.appendChild(td);
        }
        
        excelTable.appendChild(row);
    }
    
    // 显示分页控件
    displayExcelPagination();
}

// 显示Excel分页控件
function displayExcelPagination() {
    const paginationContainer = document.getElementById('excel-pagination');
    if (!paginationContainer) {
        // 如果分页容器不存在，创建一个
        const excelPreview = document.getElementById('excel-preview');
        if (excelPreview) {
            const container = document.createElement('div');
            container.id = 'excel-pagination';
            container.className = 'pagination-container';
            container.style.cssText = 'display: flex; justify-content: center; align-items: center; margin-top: 20px; gap: 10px;';
            excelPreview.appendChild(container);
        }
    }
    
    const container = document.getElementById('excel-pagination');
    if (!container) return;
    
    container.innerHTML = '';
    
    // 总行数信息
    const infoSpan = document.createElement('span');
    infoSpan.textContent = `共 ${excelTotalRows} 行`;
    infoSpan.style.color = '#6c757d';
    container.appendChild(infoSpan);
    
    // 上一页按钮
    const prevBtn = document.createElement('button');
    prevBtn.textContent = '上一页';
    prevBtn.className = 'pagination-btn';
    prevBtn.disabled = excelCurrentPage <= 1;
    prevBtn.onclick = () => loadExcelPage(excelCurrentPage - 1);
    container.appendChild(prevBtn);
    
    // 页码信息
    const pageSpan = document.createElement('span');
    pageSpan.textContent = `${excelCurrentPage} / ${excelTotalPages}`;
    pageSpan.style.margin = '0 10px';
    container.appendChild(pageSpan);
    
    // 下一页按钮
    const nextBtn = document.createElement('button');
    nextBtn.textContent = '下一页';
    nextBtn.className = 'pagination-btn';
    nextBtn.disabled = excelCurrentPage >= excelTotalPages;
    nextBtn.onclick = () => loadExcelPage(excelCurrentPage + 1);
    container.appendChild(nextBtn);
    
    // 跳转到指定页
    const jumpInput = document.createElement('input');
    jumpInput.type = 'number';
    jumpInput.min = '1';
    jumpInput.max = excelTotalPages.toString();
    jumpInput.value = excelCurrentPage.toString();
    jumpInput.style.width = '60px';
    jumpInput.style.padding = '5px';
    container.appendChild(jumpInput);
    
    const jumpBtn = document.createElement('button');
    jumpBtn.textContent = '跳转';
    jumpBtn.className = 'pagination-btn';
    jumpBtn.onclick = () => {
        const page = parseInt(jumpInput.value);
        if (page >= 1 && page <= excelTotalPages) {
            loadExcelPage(page);
        }
    };
    container.appendChild(jumpBtn);
}

// 加载Excel指定页数据
async function loadExcelPage(pageNumber) {
    if (!APP_STATE.currentFile || !APP_STATE.currentFile.id) {
        console.warn('没有当前文件');
        return;
    }
    
    try {
        showLoading('加载中...');
        
        const response = await FileAPI.previewExcel(APP_STATE.currentFile.id, pageNumber, PREVIEW_PAGE_SIZE);
        
        if (response.success && response.result && response.result.excelData && response.result.excelData.sheets) {
            // 找到当前工作表
            const sheets = response.result.excelData.sheets;
            const currentSheet = sheets.find(s => s.name === currentSheetName);
            
            if (currentSheet) {
                switchSheet(currentSheet, sheets.indexOf(currentSheet));
            }
        }
    } catch (error) {
        console.error('加载Excel页面失败:', error);
        showMessage('加载失败: ' + error.message, 'error');
    } finally {
        hideLoading();
    }
}

// 将列索引转换为Excel列号（A, B, C, ...）
function getColumnLetter(colIndex) {
    let result = '';
    let index = colIndex;
    
    while (index >= 0) {
        result = String.fromCharCode(65 + (index % 26)) + result;
        index = Math.floor(index / 26) - 1;
    }
    
    return result || 'A';
}

// 验证文件类型
function validateFileType(file) {
    const allowedTypes = CONFIG.UPLOAD.ALLOWED_TYPES || ['.xlsx'];
    const fileName = file.name.toLowerCase();
    return allowedTypes.some(type => fileName.endsWith(type));
}

// 验证文件大小
function validateFileSize(file) {
    const maxSize = CONFIG.UPLOAD.MAX_FILE_SIZE || 10 * 1024 * 1024; // 默认10MB
    return file.size <= maxSize;
}

// 格式化文件大小
function formatFileSize(bytes) {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// 处理文件上传
async function handleFileUpload(file) {
    // 验证文件类型
    if (!validateFileType(file)) {
        showMessage('仅支持.xlsx格式的Excel文件', 'error');
        return;
    }
    
    // 验证文件大小
    if (!validateFileSize(file)) {
        showMessage(`文件大小不能超过${formatFileSize(CONFIG.UPLOAD.MAX_FILE_SIZE)}`, 'error');
        return;
    }
    
    try {
        showLoading('上传文件中...');
        
        // 1. 上传文件信息
        const fileInfo = {
            filename: file.name,
            fileSize: file.size,
            fileExt: 'xlsx'
        };
        
        const fileInfoResponse = await FileAPI.uploadFileInfo(fileInfo);
        
        if (!fileInfoResponse.success) {
            throw new Error(fileInfoResponse.message || '上传文件信息失败');
        }
        
        const fileId = fileInfoResponse.result.fileId;
        
        // 2. 上传文件数据
        const uploadResponse = await FileAPI.uploadFile(file, fileId);
        
        if (!uploadResponse.success) {
            throw new Error(uploadResponse.message || '上传文件数据失败');
        }
        
        // 3. 预览Excel文件
        const previewResponse = await FileAPI.previewExcel(fileId);
        
        if (previewResponse.success) {
            displayExcelPreview(previewResponse.result);
            
            // 【修复】确保文件信息已保存到APP_STATE.currentFile（displayExcelPreview已经做了，但这里再次确认）
            if (typeof APP_STATE !== 'undefined' && APP_STATE) {
                if (!APP_STATE.currentFile || APP_STATE.currentFile.id !== fileId) {
                    APP_STATE.currentFile = {
                        id: fileId,
                        name: previewResponse.result.fileName || file.name,
                        size: previewResponse.result.fileSize || file.size,
                        data: previewResponse.result
                    };
                    console.log('文件信息已保存到APP_STATE.currentFile:', APP_STATE.currentFile);
                }
            }
            
            showMessage('文件上传成功', 'success');
        } else {
            // 预览失败，但文件上传成功，显示成功消息和预览失败提示
            console.warn('文件预览失败:', previewResponse.message || '未知错误');
            
            // 【修复】即使预览失败，也要保存文件基本信息，以便后续关联
            if (typeof APP_STATE !== 'undefined' && APP_STATE) {
                APP_STATE.currentFile = {
                    id: fileId,
                    name: file.name,
                    size: file.size,
                    data: null  // 预览失败，没有预览数据
                };
                console.log('文件基本信息已保存（预览失败）:', APP_STATE.currentFile);
            }
            
            showMessage('文件上传成功，但预览功能暂时不可用', 'warning');
        }
        
        // 4. 【修复】如果存在当前会话，将文件与会话关联（先创建会话，后上传文件的情况）
        console.log('========== 开始检查是否需要关联文件和会话 ==========');
        console.log('APP_STATE 存在:', typeof APP_STATE !== 'undefined' && APP_STATE);
        console.log('APP_STATE.currentSession 存在:', APP_STATE && APP_STATE.currentSession);
        console.log('APP_STATE.currentSession.id 存在:', APP_STATE && APP_STATE.currentSession && APP_STATE.currentSession.id);
        console.log('APP_STATE.currentSession 完整对象:', APP_STATE && APP_STATE.currentSession);
        
        if (typeof APP_STATE !== 'undefined' && APP_STATE && APP_STATE.currentSession && APP_STATE.currentSession.id) {
            try {
                const chatSessionId = APP_STATE.currentSession.id;
                console.log('========== 开始关联文件和会话（先创建会话，后上传文件） ==========');
                console.log('文件ID:', fileId);
                console.log('会话ID:', chatSessionId);
                console.log('准备发送 handleFileChatSessionMap 请求...');
                
                const mapResponse = await FileAPI.handleFileChatSessionMap(fileId, chatSessionId);
                console.log('关联请求响应:', mapResponse);
                
                if (mapResponse && mapResponse.success) {
                    console.log('✓✓✓ 文件和会话关联成功（后端确认）');
                    
                    // 同步前端会话状态，确保后续消息按照标签解析
                    // 再次检查 currentSession 是否存在（防止异步操作期间被清空）
                    if (typeof APP_STATE !== 'undefined' && APP_STATE && APP_STATE.currentSession) {
                        APP_STATE.currentSession.fileId = fileId;
                        console.log('✓✓✓ 已更新前端会话的fileId:', fileId);
                        console.log('✓✓✓ 当前会话对象:', APP_STATE.currentSession);
                        console.log('✓✓✓ 验证fileId是否已设置:', APP_STATE.currentSession.fileId === fileId);
                        console.log('✓✓✓ 验证会话ID是否匹配:', APP_STATE.currentSession.id === chatSessionId);
                        
                        // 文件关联成功后，禁用上传按钮
                        if (typeof updateUploadButtonState === 'function') {
                            updateUploadButtonState();
                        }
                    } else {
                        console.error('✗✗✗ 会话已被清空，无法更新fileId');
                    }
                } else {
                    console.error('✗✗✗ 文件和会话关联失败（后端返回失败）');
                    console.error('响应详情:', mapResponse);
                    console.error('错误信息:', mapResponse ? (mapResponse.message || mapResponse.errorMsg || '未知错误') : '响应为空');
                    // 不显示错误消息，因为文件上传已经成功
                }
            } catch (error) {
                console.error('✗✗✗ 关联文件和会话时出错:', error);
                console.error('错误堆栈:', error.stack);
                // 不显示错误消息，因为文件上传已经成功
            }
        } else {
            // 【修复】当前没有会话（先上传文件，后创建会话的情况）
            // 文件信息已经保存到APP_STATE.currentFile，等待用户创建会话后自动关联
            console.log('========== 当前没有会话，跳过关联 ==========');
            console.log('文件信息已保存到 APP_STATE.currentFile，等待用户创建会话后自动关联');
            console.log('保存的文件信息:', APP_STATE.currentFile);
            
            // 详细记录为什么没有关联
            if (typeof APP_STATE === 'undefined' || !APP_STATE) {
                console.warn('原因: APP_STATE 未定义');
            } else if (!APP_STATE.currentSession) {
                console.warn('原因: APP_STATE.currentSession 不存在');
            } else if (!APP_STATE.currentSession.id) {
                console.warn('原因: APP_STATE.currentSession.id 不存在');
                console.warn('currentSession 对象:', APP_STATE.currentSession);
            }
        }
        
    } catch (error) {
        console.error('文件上传失败:', error);
        showMessage(`文件上传失败: ${error.message}`, 'error');
    } finally {
        hideLoading();
    }
}

// 处理上传按钮点击
function handleUploadButtonClick(e) {
    if (e) {
        e.preventDefault();
        e.stopPropagation();
    }
    
    const fileInput = document.getElementById('file-upload');
    if (!fileInput) {
        return;
    }
    
    // 如果文件输入框已经有文件，先清空
    if (fileInput.value) {
        fileInput.value = '';
    }
    
    // 直接触发文件选择对话框
    fileInput.click();
}

// 处理文件输入变化
function handleFileInputChange(e) {
    const file = e.target.files[0];
    if (!file) {
        return;
    }
    
    // 立即清空文件输入，防止重复触发
    e.target.value = '';
    
    // 直接处理文件上传，不需要setTimeout
    handleFileUpload(file);
}

// 控制上传按钮的启用/禁用状态
function updateUploadButtonState() {
    const uploadBtn = document.getElementById('upload-btn');
    if (!uploadBtn) {
        console.warn('上传按钮未找到');
        return;
    }
    
    // 检查是否有当前会话
    const hasSession = APP_STATE && APP_STATE.currentSession && APP_STATE.currentSession.id;
    
    // 检查会话是否已关联文件
    const hasFile = hasSession && APP_STATE.currentSession.fileId;
    
    // 详细日志
    console.log('========== 更新上传按钮状态 ==========');
    console.log('APP_STATE 存在:', !!APP_STATE);
    console.log('currentSession 存在:', !!(APP_STATE && APP_STATE.currentSession));
    console.log('currentSession.id:', APP_STATE && APP_STATE.currentSession ? APP_STATE.currentSession.id : 'N/A');
    console.log('currentSession.fileId:', APP_STATE && APP_STATE.currentSession ? APP_STATE.currentSession.fileId : 'N/A');
    console.log('hasSession:', hasSession);
    console.log('hasFile:', hasFile);
    
    // 如果有会话且未关联文件，则启用上传按钮；否则禁用
    if (hasSession && !hasFile) {
        uploadBtn.disabled = false;
        uploadBtn.style.opacity = '1';
        uploadBtn.style.cursor = 'pointer';
        uploadBtn.title = '上传文件';
        console.log('✓ 上传按钮已启用（有会话且未关联文件）');
    } else {
        uploadBtn.disabled = true;
        uploadBtn.style.opacity = '0.5';
        uploadBtn.style.cursor = 'not-allowed';
        if (!hasSession) {
            uploadBtn.title = '请先创建会话';
            console.log('✗ 上传按钮已禁用（没有会话）');
        } else if (hasFile) {
            uploadBtn.title = '该会话已关联文件，无法再次上传';
            console.log('✗ 上传按钮已禁用（会话已关联文件）');
        } else {
            console.log('✗ 上传按钮已禁用（未知原因）');
        }
    }
    
    console.log('上传按钮最终状态:', {
        disabled: uploadBtn.disabled,
        opacity: uploadBtn.style.opacity,
        title: uploadBtn.title
    });
    console.log('=====================================');
}

// 保存事件处理函数引用，用于正确移除事件监听器
let uploadButtonClickHandler = null;
let fileInputChangeHandler = null;

// 绑定文件上传事件
function bindFileUploadEvents() {
    const uploadBtn = document.getElementById('upload-btn');
    const fileInput = document.getElementById('file-upload');
    
    if (!uploadBtn || !fileInput) {
        console.warn('文件上传按钮或文件输入框未找到，将在1秒后重试');
        setTimeout(bindFileUploadEvents, 1000);
        return;
    }
    
    // 移除之前的事件监听器（避免重复绑定）
    if (uploadButtonClickHandler) {
        uploadBtn.removeEventListener('click', uploadButtonClickHandler);
    }
    if (fileInputChangeHandler) {
        fileInput.removeEventListener('change', fileInputChangeHandler);
    }
    
    // 创建新的事件处理函数并保存引用
    uploadButtonClickHandler = (e) => {
        // 如果按钮被禁用，阻止点击
        if (uploadBtn.disabled) {
            e.preventDefault();
            e.stopPropagation();
            if (!APP_STATE || !APP_STATE.currentSession || !APP_STATE.currentSession.id) {
                showMessage('请先创建会话后再上传文件', 'warning');
            } else if (APP_STATE.currentSession.fileId) {
                showMessage('该会话已关联文件，无法再次上传', 'warning');
            }
            return;
        }
        handleUploadButtonClick(e);
    };
    
    fileInputChangeHandler = handleFileInputChange;
    
    // 绑定新的事件监听器
    uploadBtn.addEventListener('click', uploadButtonClickHandler);
    fileInput.addEventListener('change', fileInputChangeHandler);
    
    // 初始化时禁用上传按钮（默认状态）
    updateUploadButtonState();
}

// 初始化文件上传功能
function initFileUpload() {
    // 等待DOM完全加载后再绑定事件
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', () => {
            bindFileUploadEvents();
        });
    } else {
        // DOM已经加载完成，直接绑定事件
        bindFileUploadEvents();
    }
    
    // 拖放上传功能
    const dropZone = document.querySelector('.file-preview-container');
    if (dropZone) {
        // 阻止默认拖放行为
        ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
            dropZone.addEventListener(eventName, preventDefaults, false);
        });
        
        // 高亮拖放区域
        ['dragenter', 'dragover'].forEach(eventName => {
            dropZone.addEventListener(eventName, highlight, false);
        });
        
        ['dragleave', 'drop'].forEach(eventName => {
            dropZone.addEventListener(eventName, unhighlight, false);
        });
        
        // 处理文件拖放
        dropZone.addEventListener('drop', handleDrop, false);
    }
}

// 阻止默认事件
function preventDefaults(e) {
    e.preventDefault();
    e.stopPropagation();
}

// 高亮拖放区域
function highlight() {
    const dropZone = document.querySelector('.file-preview-container');
    if (dropZone) {
        dropZone.style.border = '2px dashed #667eea';
        dropZone.style.backgroundColor = '#f0f4ff';
    }
}

// 取消高亮拖放区域
function unhighlight() {
    const dropZone = document.querySelector('.file-preview-container');
    if (dropZone) {
        dropZone.style.border = '';
        dropZone.style.backgroundColor = '';
    }
}

// 处理文件拖放
function handleDrop(e) {
    const dt = e.dataTransfer;
    const files = dt.files;
    
    if (files.length > 0) {
        // 检查上传按钮是否被禁用
        const uploadBtn = document.getElementById('upload-btn');
        if (uploadBtn && uploadBtn.disabled) {
            e.preventDefault();
            e.stopPropagation();
            if (!APP_STATE || !APP_STATE.currentSession || !APP_STATE.currentSession.id) {
                showMessage('请先创建会话后再上传文件', 'warning');
            } else if (APP_STATE.currentSession.fileId) {
                showMessage('该会话已关联文件，无法再次上传', 'warning');
            }
            return;
        }
        handleFileUpload(files[0]);
    }
}

// 初始化文件功能
function initFileFunctions() {
    initFileUpload();
}

// 暴露更新上传按钮状态的函数到全局作用域
window.updateUploadButtonState = updateUploadButtonState;

// 页面加载完成后初始化文件功能
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initFileFunctions);
} else {
    initFileFunctions();
}