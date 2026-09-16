// 图表渲染模块

// 如果全局不存在 isBlobType 和 decodeBase64ToHex，则提供本地实现
if (typeof isBlobType !== 'function') {
    function isBlobType(columnType) {
        if (!columnType || typeof columnType !== 'string') {
            return false;
        }
        const upperType = columnType.toUpperCase();
        return upperType.includes('BLOB') || upperType.includes('BINARY') || upperType.includes('VARBINARY');
    }
}

if (typeof decodeBase64ToHex !== 'function') {
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
}

// 按优先级尝试：同源本地 -> 国内 CDN -> 国际 CDN（避免 CSP/内网/跨域导致 CDN 失败）
var ECHARTS_SOURCES = [
    'js/echarts.min.js',
    'https://cdn.bootcdn.net/ajax/libs/echarts/5.4.3/echarts.min.js',
    'https://unpkg.com/echarts@5.4.3/dist/echarts.min.js',
    'https://cdn.jsdelivr.net/npm/echarts@5.4.3/dist/echarts.min.js'
];
var echartsLoadPromise = null;

// 获取 ECharts 实例（兼容 CDN 延迟或不同全局环境）
function getEcharts() {
    if (typeof window !== 'undefined' && window.echarts) return window.echarts;
    if (typeof echarts !== 'undefined') return echarts;
    return null;
}

// 尝试从指定 URL 加载一个 script，返回 Promise
function loadScriptOnce(url) {
    return new Promise(function(resolve, reject) {
        var s = document.createElement('script');
        s.src = url;
        s.async = false;
        s.onload = function() {
            var lib = getEcharts();
            if (lib) resolve(lib);
            else reject(new Error('脚本已加载但 ECharts 未就绪'));
        };
        s.onerror = function() { reject(new Error('加载失败: ' + url)); };
        (document.head || document.documentElement).appendChild(s);
    });
}

// 动态加载 ECharts：依次尝试 ECHARTS_SOURCES，直到成功或全部失败
function loadEchartsScript() {
    if (getEcharts()) return Promise.resolve(getEcharts());
    if (echartsLoadPromise) return echartsLoadPromise;
    var urls = ECHARTS_SOURCES.slice();
    var index = 0;
    function tryNext() {
        if (index >= urls.length) {
            return Promise.reject(new Error('所有 ECharts 源均加载失败（本地与 CDN），请将 echarts.min.js 放到 js/ 目录'));
        }
        var url = urls[index++];
        console.log('尝试加载 ECharts:', url);
        return loadScriptOnce(url).then(function(lib) {
            console.log('ECharts 加载成功');
            return lib;
        }).catch(function(err) {
            console.warn('ECharts 源失败:', err.message);
            return tryNext();
        });
    }
    echartsLoadPromise = tryNext();
    return echartsLoadPromise;
}

// 需要 ECharts 的图表类型
var ECHARTS_CHART_TYPES = ['BarChart', 'ColumnChart', 'LineChart', 'AreaChart', 'PieChart', 'DonutChart', 'ScatterChart'];

// 渲染图表的主函数（暴露为全局函数）
// _retried: 内部用，ECharts 未就绪时是否已重试过
window.renderChart = function(chartData, containerId, _retried) {
    console.log('========== renderChart 被调用 ==========');
    console.log('containerId:', containerId);
    console.log('chartData:', chartData);
    
    const container = document.getElementById(containerId);
    if (!container) {
        console.error('✗ 图表容器不存在:', containerId);
        console.error('  可能的原因：容器 ID 不正确或容器未添加到 DOM');
        return;
    }
    
    console.log('✓ 容器找到:', container);
    
    const { chartType, chartConfig, data, summary } = chartData;
    
    // 若该类型依赖 ECharts 且未加载：先尝试动态加载，再重试渲染
    if (chartType && ECHARTS_CHART_TYPES.indexOf(chartType) !== -1 && !getEcharts() && !_retried) {
        console.warn('ECharts 未就绪，尝试动态加载...');
        loadEchartsScript().then(function() {
            window.renderChart(chartData, containerId, true);
        }).catch(function(err) {
            console.error('ECharts 加载失败:', err);
            container.innerHTML = '<p style="color:#c00; padding:12px;">图表库加载失败: ' + (err && err.message ? err.message : '请检查网络后刷新') + '</p>';
        });
        return;
    }
    
    console.log('图表类型:', chartType);
    console.log('是否有数据:', !!data);
    console.log('是否有列:', data && !!data.columns);
    console.log('是否有行:', data && !!data.rows);
    
    // 如果是多表结果（来自 executeModificationSQL 的多表临时表），优先按 Excel 形式渲染
    const tables = data && Array.isArray(data.tables) ? data.tables : null;
    if (tables && tables.length > 0) {
        console.log('检测到多表结果，按 Excel 形式渲染，多表数量:', tables.length);
        if (typeof renderMultiTableExcelView === 'function') {
            renderMultiTableExcelView(data, container);
        } else {
            console.warn('renderMultiTableExcelView 未定义，回退到单表 Table 渲染');
            renderTable(
                {
                    columns: tables[0].columns || [],
                    rows: tables[0].rows || []
                },
                container
            );
        }
        return;
    }

    // 显示总结文本
    if (summary) {
        const summaryDiv = document.createElement('div');
        summaryDiv.className = 'chart-summary';
        summaryDiv.style.cssText = 'margin: 10px 0; padding: 10px; background: #f0f9ff; border-left: 3px solid #0ea5e9; border-radius: 4px;';
        summaryDiv.textContent = summary;
        container.appendChild(summaryDiv);
    }
    
    // 创建图表容器
    const chartDiv = document.createElement('div');
    chartDiv.id = `chart-${Date.now()}`;
    // 【修复】确保图表容器有足够的宽度和高度
    // 先添加到DOM，然后获取实际尺寸
    chartDiv.style.cssText = 'width: 100%; min-width: 600px; height: 500px; min-height: 500px; margin: 20px 0; box-sizing: border-box;';
    container.appendChild(chartDiv);
    
    // 等待DOM更新后，获取实际尺寸并调整
    setTimeout(() => {
        const actualWidth = container.offsetWidth || container.clientWidth || 600;
        const actualHeight = Math.max(container.offsetHeight || container.clientHeight || 500, 500);
        if (actualWidth > 0 && actualHeight > 0) {
            chartDiv.style.width = `${actualWidth}px`;
            chartDiv.style.height = `${actualHeight}px`;
        }
    }, 50);
    
    try {
        switch (chartType) {
            case 'Table':
                renderTable(data, chartDiv);
                break;
            case 'BarChart':
            case 'ColumnChart':
                renderBarChart(data, chartConfig, chartDiv, chartType === 'ColumnChart');
                break;
            case 'LineChart':
            case 'AreaChart':
                renderLineChart(data, chartConfig, chartDiv, chartType === 'AreaChart');
                break;
            case 'PieChart':
            case 'DonutChart':
                renderPieChart(data, chartConfig, chartDiv, chartType === 'DonutChart');
                break;
            case 'ScatterChart':
                renderScatterChart(data, chartConfig, chartDiv);
                break;
            case 'NumberDisplay':
                renderNumberDisplay(data, chartDiv);
                break;
            default:
                renderTable(data, chartDiv);
        }
    } catch (error) {
        console.error('渲染图表失败:', error);
        chartDiv.innerHTML = '<p style="color: red;">图表渲染失败: ' + error.message + '</p>';
    }
};

// 渲染多表 Excel 风格视图（用于 executeModificationSQL 返回的多表结果）
function renderMultiTableExcelView(data, container) {
    const tables = Array.isArray(data.tables) ? data.tables : [];
    if (!tables.length) {
        console.warn('renderMultiTableExcelView: tables 为空');
        return;
    }

    // 获取列类型（来自 SQL 执行结果的 data.columnTypes）
    const globalColumnTypes = data.columnTypes || [];

    // 外层容器
    const wrapper = document.createElement('div');
    wrapper.className = 'multi-table-excel-view';
    wrapper.style.cssText = 'margin-top: 10px; border: 1px solid #e5e7eb; border-radius: 6px; background: #f9fafb; overflow: hidden;';

    // 标题
    const header = document.createElement('div');
    header.className = 'excel-header';
    header.style.cssText = 'display: flex; align-items: center; justify-content: space-between; padding: 8px 12px; background: #f3f4f6; border-bottom: 1px solid #e5e7eb;';
    header.innerHTML = `<div style="font-weight: 600; color: #111827;">多表结果预览（${tables.length} 张表）</div>
                        <div style="font-size: 12px; color: #6b7280;">数据来自修改类 SQL 的多个临时表</div>`;
    wrapper.appendChild(header);

    // sheet 标签
    const sheetTabs = document.createElement('div');
    sheetTabs.className = 'sheet-tabs';
    sheetTabs.style.cssText = 'display: flex; gap: 4px; padding: 6px 8px; background: #e5e7eb; border-bottom: 1px solid #d1d5db;';
    wrapper.appendChild(sheetTabs);

    // 表格容器
    const tableContainer = document.createElement('div');
    tableContainer.className = 'excel-table-container';
    tableContainer.style.cssText = 'max-height: 500px; overflow: auto; background: white;';

    const table = document.createElement('table');
    table.className = 'excel-table';
    table.style.cssText = 'width: 100%; border-collapse: collapse; font-size: 12px; min-width: 100%;';
    tableContainer.appendChild(table);
    wrapper.appendChild(tableContainer);

    container.appendChild(wrapper);

    function renderOneTable(index) {
        const tableInfo = tables[index];
        const rows = Array.isArray(tableInfo.rows) ? tableInfo.rows : [];
        const columns = Array.isArray(tableInfo.columns) ? tableInfo.columns : [];
        // 优先使用columns长度，如果没有columns则从第一行数据获取列数
        const colCount = columns.length > 0 ? columns.length : (rows.length > 0 && rows[0] ? rows[0].length : 0);

        table.innerHTML = '';

        // 如果没有列，显示空表提示
        if (colCount === 0) {
            const emptyRow = document.createElement('tr');
            const emptyCell = document.createElement('td');
            emptyCell.colSpan = 2;
            emptyCell.style.cssText = 'padding: 20px; text-align: center; color: #6b7280; font-style: italic;';
            emptyCell.textContent = '该表没有数据';
            emptyRow.appendChild(emptyCell);
            table.appendChild(emptyRow);
            return;
        }

        // 表头：左上角 + 列号（A、B、C...）
        const headerRow = document.createElement('tr');
        const cornerCell = document.createElement('th');
        cornerCell.className = 'corner-cell';
        cornerCell.style.cssText = 'width: 40px; min-width: 40px; background: #f3f4f6; border: 1px solid #e5e7eb; position: sticky; left: 0; z-index: 10;';
        headerRow.appendChild(cornerCell);

        for (let c = 0; c < colCount; c++) {
            const th = document.createElement('th');
            th.className = 'column-header';
            th.style.cssText = 'padding: 4px 6px; background: #f3f4f6; border: 1px solid #e5e7eb; font-weight: 600; text-align: center; min-width: 80px; max-width: 200px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;';
            // 如果有列名，显示列名；否则显示列号
            if (columns.length > c && columns[c]) {
                th.textContent = columns[c];
                th.title = columns[c];
            } else {
                if (typeof getColumnLetter === 'function') {
                    th.textContent = getColumnLetter(c);
                } else {
                    th.textContent = String.fromCharCode(65 + (c % 26));
                }
            }
            headerRow.appendChild(th);
        }
        table.appendChild(headerRow);

        // 如果没有数据行，显示提示
        if (rows.length === 0) {
            const emptyRow = document.createElement('tr');
            const emptyCell = document.createElement('td');
            emptyCell.colSpan = colCount + 1;
            emptyCell.style.cssText = 'padding: 20px; text-align: center; color: #6b7280; font-style: italic; background: #f9fafb;';
            emptyCell.textContent = `该表没有数据（rowCount: ${tableInfo.rowCount || 0}）`;
            emptyRow.appendChild(emptyCell);
            table.appendChild(emptyRow);
            return;
        }

        // 数据行（带行号）
        const maxRows = Math.min(rows.length, 100); // 增加到100行
        for (let r = 0; r < maxRows; r++) {
            const tr = document.createElement('tr');

            const rowHeader = document.createElement('th');
            rowHeader.className = 'row-header';
            rowHeader.style.cssText = 'width: 40px; min-width: 40px; background: #f9fafb; border: 1px solid #e5e7eb; text-align: right; padding: 2px 6px; color: #6b7280; position: sticky; left: 0; z-index: 5;';
            rowHeader.textContent = (r + 1).toString();
            tr.appendChild(rowHeader);

            const rowData = Array.isArray(rows[r]) ? rows[r] : [];
            // 优先使用 tableInfo.columnTypes，否则使用 globalColumnTypes（来自 SQL 执行结果）
            const columnTypes = tableInfo.columnTypes || globalColumnTypes;
            for (let c = 0; c < colCount; c++) {
                const td = document.createElement('td');
                td.style.cssText = 'padding: 4px 6px; border: 1px solid #e5e7eb; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 200px;';
                const colType = columnTypes[c];
                const cellValue = rowData[c] != null ? String(rowData[c]) : '';
                // 根据列类型判断是否为 BLOB 类型，如果是则进行 base64 解码
                const displayValue = (isBlobType(colType) && cellValue) ? decodeBase64ToHex(cellValue) : cellValue;
                td.textContent = displayValue.length > 100 ? (displayValue.substring(0, 100) + '...') : displayValue;
                if (displayValue.length > 100) {
                    td.title = displayValue;
                }
                tr.appendChild(td);
            }

            table.appendChild(tr);
        }

        // 额外信息行
        if (rows.length > maxRows) {
            const infoRow = document.createElement('tr');
            const infoCell = document.createElement('td');
            infoCell.colSpan = colCount + 1;
            infoCell.style.cssText = 'padding: 4px 6px; text-align: center; color: #6b7280; font-style: italic; background: #f9fafb;';
            infoCell.textContent = `... 仅显示前 ${maxRows} 行，共 ${rows.length} 行`;
            infoRow.appendChild(infoCell);
            table.appendChild(infoRow);
        }
    }

    // 创建 tabs
    tables.forEach((t, index) => {
        const tab = document.createElement('button');
        tab.className = 'sheet-tab' + (index === 0 ? ' active' : '');
        tab.style.cssText = 'padding: 4px 10px; border-radius: 4px; border: none; background: ' +
            (index === 0 ? '#ffffff' : 'transparent') +
            '; cursor: pointer; font-size: 12px; color: #111827;';
        tab.textContent = t.tempTable || `表${index + 1}`;

        tab.addEventListener('click', () => {
            // 切换激活状态
            sheetTabs.querySelectorAll('button').forEach(btn => {
                btn.classList.remove('active');
                btn.style.background = 'transparent';
            });
            tab.classList.add('active');
            tab.style.background = '#ffffff';

            // 渲染对应表
            renderOneTable(index);
        });

        sheetTabs.appendChild(tab);
    });

    // 默认显示第一张表
    renderOneTable(0);
}

// 渲染表格
function renderTable(data, container) {
    const { columns, columnTypes, rows } = data;
    
    let html = '<div style="overflow-x: auto;"><table style="width: 100%; border-collapse: collapse; margin: 10px 0;">';
    
    // 表头
    html += '<thead><tr style="background: #f3f4f6;">';
    columns.forEach(col => {
        html += `<th style="padding: 12px; text-align: left; border: 1px solid #e5e7eb;">${col}</th>`;
    });
    html += '</tr></thead>';
    
    // 表体
    html += '<tbody>';
    rows.forEach((row, index) => {
        html += `<tr style="background: ${index % 2 === 0 ? '#ffffff' : '#f9fafb'};">`;
        row.forEach((cell, colIndex) => {
            const colType = columnTypes && columnTypes[colIndex];
            const cellStr = cell != null ? String(cell) : '';
            // 根据列类型判断是否为 BLOB 类型，如果是则进行 base64 解码
            const displayValue = (isBlobType(colType) && cellStr) ? decodeBase64ToHex(cellStr) : cellStr;
            html += `<td style="padding: 12px; border: 1px solid #e5e7eb;">${displayValue}</td>`;
        });
        html += '</tr>';
    });
    html += '</tbody></table></div>';
    
    container.innerHTML = html;
}

// 渲染柱状图/条形图
function renderBarChart(data, config, container, isColumn) {
    const echartsLib = getEcharts();
    if (!echartsLib) {
        console.error('ECharts 未加载，请检查 index.html 中是否引入 echarts.min.js 或网络是否正常');
        if (container) container.innerHTML = '<p style="color:#c00; padding:12px;">图表库未加载，请检查网络后刷新页面</p>';
        return;
    }
    const { columns, rows } = data;
    const chart = echartsLib.init(container);
    
    // 确定X轴和Y轴数据
    const xAxisIndex = typeof config.xAxis === 'number' ? config.xAxis : 0;
    const yAxisIndex = typeof config.yAxis === 'number' ? config.yAxis : 1;
    
    const categories = rows.map(row => row[xAxisIndex]);
    const values = rows.map(row => parseFloat(row[yAxisIndex]) || 0);
    
    const option = {
        title: { text: config.title || '数据分析', left: 'center' },
        tooltip: { trigger: 'axis' },
        grid: {
            left: '10%',
            right: '10%',
            top: '15%',
            bottom: '20%',
            containLabel: true
        },
        xAxis: isColumn ? {
            type: 'category',
            data: categories,
            axisLabel: { 
                interval: 0, 
                rotate: categories.length > 10 ? 45 : 30,
                fontSize: 12,
                margin: 10
            }
        } : {
            type: 'value'
        },
        yAxis: isColumn ? {
            type: 'value'
        } : {
            type: 'category',
            data: categories,
            axisLabel: {
                fontSize: 12,
                margin: 10
            }
        },
        series: [{
            name: columns[yAxisIndex],
            type: 'bar',
            data: values,
            itemStyle: { color: '#0ea5e9' },
            label: { 
                show: true, 
                position: isColumn ? 'top' : 'right',
                fontSize: 11,
                formatter: function(params) {
                    // 如果数值太大，显示简化格式
                    if (params.value > 1000) {
                        return (params.value / 1000).toFixed(1) + 'k';
                    }
                    return params.value;
                }
            }
        }]
    };
    
    chart.setOption(option);
    window.addEventListener('resize', () => chart.resize());
    // 【修复问题1】渲染后延迟调用resize，确保容器尺寸已计算
    setTimeout(() => {
        chart.resize();
    }, 100);
}

// 渲染折线图/面积图
function renderLineChart(data, config, container, isArea) {
    const echartsLib = getEcharts();
    if (!echartsLib) {
        console.error('ECharts 未加载');
        if (container) container.innerHTML = '<p style="color:#c00; padding:12px;">图表库未加载，请检查网络后刷新页面</p>';
        return;
    }
    const { columns, rows } = data;
    const chart = echartsLib.init(container);
    
    const xAxisIndex = typeof config.xAxis === 'number' ? config.xAxis : 0;
    const yAxisIndex = typeof config.yAxis === 'number' ? config.yAxis : 1;
    
    const categories = rows.map(row => row[xAxisIndex]);
    const values = rows.map(row => parseFloat(row[yAxisIndex]) || 0);
    
    const option = {
        title: { text: config.title || '趋势分析', left: 'center' },
        tooltip: { trigger: 'axis' },
        grid: {
            left: '10%',
            right: '10%',
            top: '15%',
            bottom: '20%',
            containLabel: true
        },
        xAxis: {
            type: 'category',
            data: categories,
            boundaryGap: false,
            axisLabel: {
                rotate: categories.length > 10 ? 45 : 0,
                fontSize: 12,
                margin: 10
            }
        },
        yAxis: { 
            type: 'value',
            axisLabel: {
                fontSize: 12
            }
        },
        series: [{
            name: columns[yAxisIndex],
            type: 'line',
            data: values,
            smooth: true,
            areaStyle: isArea ? {} : null,
            itemStyle: { color: '#10b981' },
            label: {
                show: false
            }
        }]
    };
    
    chart.setOption(option);
    window.addEventListener('resize', () => chart.resize());
    // 【修复问题1】渲染后延迟调用resize，确保容器尺寸已计算
    setTimeout(() => {
        chart.resize();
    }, 100);
}

// 渲染饼图/环形图
function renderPieChart(data, config, container, isDonut) {
    console.log('========== renderPieChart 被调用 ==========');
    console.log('data:', data);
    console.log('config:', config);
    console.log('container:', container);
    console.log('isDonut:', isDonut);
    
    const { columns, rows } = data;
    
    if (!columns || !rows) {
        console.error('✗ 数据不完整: columns=', columns, ', rows=', rows);
        return;
    }
    
    console.log('columns:', columns);
    console.log('rows 数量:', rows.length);
    console.log('rows 示例（前3条）:', rows.slice(0, 3));
    
    const echartsLib = getEcharts();
    if (!echartsLib) {
        console.error('ECharts 未加载');
        if (container) container.innerHTML = '<p style="color:#c00; padding:12px;">图表库未加载，请检查网络后刷新页面</p>';
        return;
    }
    console.log('初始化 ECharts 实例...');
    const chart = echartsLib.init(container);
    console.log('✓ ECharts 实例已创建');
    
    // 饼图数据：第一列作为名称，第二列作为数值
    const pieData = rows.map(row => ({
        name: row[0],
        value: parseFloat(row[1]) || 0
    }));
    
    console.log('饼图数据:', pieData);
    
    const option = {
        title: { text: config.title || '占比分析', left: 'center' },
        tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
        legend: { 
            orient: 'vertical', 
            left: 'left',
            itemGap: 10,
            textStyle: {
                fontSize: 12
            }
        },
        series: [{
            name: columns[1],
            type: 'pie',
            radius: isDonut ? ['40%', '70%'] : '70%',
            center: ['50%', '55%'],
            data: pieData,
            emphasis: {
                itemStyle: {
                    shadowBlur: 10,
                    shadowOffsetX: 0,
                    shadowColor: 'rgba(0, 0, 0, 0.5)'
                }
            },
            label: { 
                formatter: '{b}: {d}%',
                fontSize: 11,
                lineHeight: 14
            },
            labelLine: {
                length: 15,
                length2: 10
            }
        }]
    };
    
    console.log('饼图配置:', option);
    console.log('设置图表选项...');
    
    try {
    chart.setOption(option);
        console.log('✓✓✓ 饼图渲染成功！');
    
        // 添加窗口调整事件
    window.addEventListener('resize', () => chart.resize());
        console.log('✓ 已绑定窗口调整事件');
        // 【修复问题1】渲染后延迟调用resize，确保容器尺寸已计算
        setTimeout(() => {
            chart.resize();
        }, 100);
    } catch (e) {
        console.error('✗✗✗ 饼图渲染失败:', e);
    }
}

// 渲染散点图
function renderScatterChart(data, config, container) {
    const echartsLib = getEcharts();
    if (!echartsLib) {
        console.error('ECharts 未加载');
        if (container) container.innerHTML = '<p style="color:#c00; padding:12px;">图表库未加载，请检查网络后刷新页面</p>';
        return;
    }
    const { columns, rows } = data;
    const chart = echartsLib.init(container);
    
    const xAxisIndex = typeof config.xAxis === 'number' ? config.xAxis : 0;
    const yAxisIndex = typeof config.yAxis === 'number' ? config.yAxis : 1;
    
    const scatterData = rows.map(row => [
        parseFloat(row[xAxisIndex]) || 0,
        parseFloat(row[yAxisIndex]) || 0
    ]);
    
    const option = {
        title: { text: config.title || '分布分析', left: 'center' },
        tooltip: { trigger: 'item' },
        grid: {
            left: '15%',
            right: '10%',
            top: '15%',
            bottom: '15%',
            containLabel: true
        },
        xAxis: { 
            type: 'value', 
            name: columns[xAxisIndex],
            nameTextStyle: {
                fontSize: 12
            },
            axisLabel: {
                fontSize: 12
            }
        },
        yAxis: { 
            type: 'value', 
            name: columns[yAxisIndex],
            nameTextStyle: {
                fontSize: 12
            },
            axisLabel: {
                fontSize: 12
            }
        },
        series: [{
            type: 'scatter',
            data: scatterData,
            itemStyle: { color: '#f59e0b' },
            symbolSize: 8
        }]
    };
    
    chart.setOption(option);
    window.addEventListener('resize', () => chart.resize());
    // 【修复问题1】渲染后延迟调用resize，确保容器尺寸已计算
    setTimeout(() => {
        chart.resize();
    }, 100);
}

// 渲染数值显示
function renderNumberDisplay(data, container) {
    const { columns, rows } = data;
    const value = rows[0] ? rows[0][0] : '0';
    const label = columns[0] || '数值';
    
    container.innerHTML = `
        <div style="text-align: center; padding: 40px;">
            <div style="font-size: 48px; font-weight: bold; color: #0ea5e9; margin-bottom: 10px;">
                ${value}
            </div>
            <div style="font-size: 18px; color: #6b7280;">
                ${label}
            </div>
        </div>
    `;
}

