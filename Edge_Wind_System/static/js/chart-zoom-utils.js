/**
 * 统一的图表缩放工具函数
 * 提供以鼠标为中心的缩放、X/Y轴独立缩放、Grab模式拖拽功能
 * 
 * 使用方法：
 *   setupChartZoom(chartInstance, chartDomElement, options)
 * 
 * 功能：
 *   1. 以鼠标为中心的缩放（滚轮在X轴区域缩放X轴，在Y轴区域缩放Y轴）
 *   2. Grab模式拖拽（鼠标向右拖拽，波形向右移动，窗口向左移动）
 *   3. 双击重置缩放（双击X轴重置X轴，双击Y轴重置Y轴，双击图表区域重置两个轴）
 */

/**
 * 设置图表缩放交互
 * @param {echarts.ECharts} chartInstance - ECharts图表实例
 * @param {HTMLElement} chartDom - 图表DOM元素
 * @param {Object} options - 配置选项
 * @param {number} options.xAxisThreshold - X轴区域阈值（底部百分比，默认0.20）
 * @param {number} options.yAxisThreshold - Y轴区域阈值（左侧百分比，默认0.18）
 * @param {number} options.zoomFactor - 缩放因子（默认1.10）
 * @param {number} options.panSpeed - 拖拽速度（默认0.5）
 */
function setupChartZoom(chartInstance, chartDom, options = {}) {
    if (!chartInstance || !chartDom) {
        console.warn('[setupChartZoom] 图表实例或DOM元素不存在');
        return;
    }

    // ==================== V2：使用 ZRender 事件（更稳定，不受 DOM 冒泡/stopPropagation 影响）====================
    // 说明：你给的 WindSight 版本之所以稳定，核心就在于：
    // - 直接用 chart.getZr() 拿到 ECharts 内部事件源
    // - 缩放/拖拽使用 dataZoom 的 startValue/endValue（真实轴值），避免“实时刷新导致缩放漂移/被覆盖”

    const config = {
        // 兼容阈值：当无法获取 gridRect 时，用百分比阈值做区域判断
        xAxisThreshold: options.xAxisThreshold || 0.20,
        yAxisThreshold: options.yAxisThreshold || 0.18,
        zoomInFactor: options.zoomInFactor || 1.10,   // 滚轮向上：放大
        zoomOutFactor: options.zoomOutFactor || 0.90, // 滚轮向下：缩小
        panSpeed: options.panSpeed || 1.0,            // Grab 拖拽速度（1.0 更贴手）
        xDataZoomIndex: (options.xDataZoomIndex !== undefined) ? options.xDataZoomIndex : 0,
        yDataZoomIndex: (options.yDataZoomIndex !== undefined) ? options.yDataZoomIndex : 1,
        gridIndex: (options.gridIndex !== undefined) ? options.gridIndex : 0,
        // “左下角交叉区域”的优先级：默认优先 Y（更符合示波器习惯）
        cornerPriority: options.cornerPriority || 'y' // 'x' | 'y' | 'none'
    };

    const zr = chartInstance.getZr ? chartInstance.getZr() : null;
    if (!zr) {
        console.warn('[setupChartZoom] 无法获取 ZRender 实例，已跳过缩放绑定');
        return;
    }

    // 清理旧绑定（避免重复绑定导致“越来越怪”）
    if (chartInstance.__edgewindZoomHandlers) {
        const old = chartInstance.__edgewindZoomHandlers;
        try { zr.off('mousewheel', old.onWheel); } catch (e) {}
        try { zr.off('mousedown', old.onDown); } catch (e) {}
        try { zr.off('mousemove', old.onMove); } catch (e) {}
        try { zr.off('mouseup', old.onUp); } catch (e) {}
        try { zr.off('globalout', old.onUp); } catch (e) {}
        try { zr.off('dblclick', old.onDblClick); } catch (e) {}
        chartInstance.__edgewindZoomHandlers = null;
    }

    function _clamp(v, min, max) {
        const n = Number(v);
        if (!Number.isFinite(n)) return min;
        return Math.max(min, Math.min(max, n));
    }

    function _getGridRect() {
        try {
            const grid = chartInstance.getModel().getComponent('grid', config.gridIndex);
            const rect = grid && grid.coordinateSystem && grid.coordinateSystem.getRect ? grid.coordinateSystem.getRect() : null;
            if (!rect) return null;
            const x = Number(rect.x), y = Number(rect.y), w = Number(rect.width), h = Number(rect.height);
            if (![x, y, w, h].every(Number.isFinite)) return null;
            if (w <= 2 || h <= 2) return null;
            return { x, y, width: w, height: h };
        } catch (e) {
            return null;
        }
    }

    function _hitRegion(px, py) {
        // 返回：'x' | 'y' | 'plot' | 'none'
        const grid = _getGridRect();
        if (grid) {
            const inPlot = (px >= grid.x && px <= grid.x + grid.width && py >= grid.y && py <= grid.y + grid.height);
            const onYAxis = (px < grid.x);
            const onXAxis = (py > grid.y + grid.height);

            // 左下角交叉区域处理
            if (onXAxis && onYAxis) {
                if (config.cornerPriority === 'x') return 'x';
                if (config.cornerPriority === 'y') return 'y';
                return 'none';
            }
            if (onYAxis) return 'y';
            if (onXAxis) return 'x';
            if (inPlot) return 'plot';
            return 'none';
        }

        // fallback：用容器百分比阈值
        const domRect = chartDom.getBoundingClientRect();
        const w = Math.max(1, domRect.width);
        const h = Math.max(1, domRect.height);
        const onXAxis = (py > h * (1 - config.xAxisThreshold));
        const onYAxis = (px < w * config.yAxisThreshold);
        if (onXAxis && onYAxis) {
            if (config.cornerPriority === 'x') return 'x';
            if (config.cornerPriority === 'y') return 'y';
            return 'none';
        }
        if (onYAxis) return 'y';
        if (onXAxis) return 'x';
        return 'plot';
    }

    function _getAxisExtent(axis) {
        // axis: 'x' | 'y'，返回 {min,max}
        // 关键说明：不要用 axis.scale.getExtent() 当作“全量范围”。
        // 在 dataZoom(filterMode) 生效后，它常常会退化成“当前窗口范围”，导致“放大后无法缩小”。
        // 因此这里优先从 option.series / xAxis.data 计算全量范围。

        // 0) 最高优先级：外部显式提供的全量范围（用于实时监测这种“X轴固定0~200ms”的场景）
        // 说明：若存在 __edgewindFullExtentX/Y，则它一定代表“全量范围”，可保证缩小能回到完整窗口。
        try {
            if (axis === 'x' && chartInstance.__edgewindFullExtentX) {
                const ex = chartInstance.__edgewindFullExtentX;
                const mn = Number(ex.min);
                const mx = Number(ex.max);
                if (Number.isFinite(mn) && Number.isFinite(mx) && mx > mn) return { min: mn, max: mx };
            }
            if (axis === 'y' && chartInstance.__edgewindFullExtentY) {
                const ey = chartInstance.__edgewindFullExtentY;
                let mn = Number(ey.min);
                let mx = Number(ey.max);
                if (Number.isFinite(mn) && Number.isFinite(mx) && mx > mn) {
                    mn = Math.min(0, mn);
                    mx = Math.max(0, mx);
                    return { min: mn, max: mx };
                }
            }
        } catch (e) {}

        function _computeFullExtentFromOption(axis2) {
            try {
                const opt = chartInstance.getOption ? chartInstance.getOption() : null;
                if (!opt) return null;

                // 1) X轴：优先使用 xAxis[0].data（category）
                if (axis2 === 'x') {
                    const xAxis = (opt.xAxis && opt.xAxis[0]) ? opt.xAxis[0] : null;
                    const xData = xAxis && Array.isArray(xAxis.data) ? xAxis.data : null;
                    if (xData && xData.length > 1) {
                        let mn = Infinity;
                        let mx = -Infinity;
                        let ok = false;
                        for (const v of xData) {
                            const n = Number(v);
                            if (!Number.isFinite(n)) continue;
                            ok = true;
                            if (n < mn) mn = n;
                            if (n > mx) mx = n;
                        }
                        if (ok && mx > mn) return { min: mn, max: mx };
                    }
                }

                // 2) 从 series.data 计算（兼容 time/[x,y]/[idx,y]/纯 y）
                const seriesArr = Array.isArray(opt.series) ? opt.series : [];
                if (!seriesArr.length) return null;

                let min = Infinity;
                let max = -Infinity;
                let ok = false;

                for (const s of seriesArr) {
                    const data = s && Array.isArray(s.data) ? s.data : [];
                    for (let i = 0; i < data.length; i++) {
                        const pt = data[i];
                        let val = null;
                        if (Array.isArray(pt)) {
                            // [x, y]
                            val = (axis2 === 'x') ? pt[0] : pt[1];
                        } else {
                            // 纯 y（category x 由 xAxis.data 给出）
                            if (axis2 === 'y') val = pt;
                        }

                        if (val === null || val === undefined) continue;

                        let n = null;
                        if (axis2 === 'x') {
                            // x 可能是时间戳/数值/可解析字符串
                            const tryNum = Number(val);
                            if (Number.isFinite(tryNum)) n = tryNum;
                            else {
                                const ts = Date.parse(String(val));
                                if (Number.isFinite(ts)) n = ts;
                            }
                        } else {
                            const tryNum = Number(val);
                            if (Number.isFinite(tryNum)) n = tryNum;
                        }

                        if (!Number.isFinite(n)) continue;
                        ok = true;
                        if (n < min) min = n;
                        if (n > max) max = n;
                    }
                }

                if (!ok || !Number.isFinite(min) || !Number.isFinite(max) || max === min) return null;

                if (axis2 === 'y') {
                    // Y 轴必须包含 0
                    min = Math.min(0, min);
                    max = Math.max(0, max);
                }

                return { min, max };
            } catch (e) {
                return null;
            }
        }

        const full = _computeFullExtentFromOption(axis);
        if (full) return full;

        try {
            const comp = chartInstance.getModel().getComponent(axis === 'x' ? 'xAxis' : 'yAxis', 0);
            const ext = comp && comp.axis && comp.axis.scale && comp.axis.scale.getExtent ? comp.axis.scale.getExtent() : null;
            if (!ext || ext.length !== 2) return null;
            let min = Number(ext[0]);
            let max = Number(ext[1]);
            if (!Number.isFinite(min) || !Number.isFinite(max) || max === min) return null;

            // 规则：时域图 Y 轴必须能看到 0（禁止 scale:true），因此把 0 纳入可用范围
            if (axis === 'y') {
                min = Math.min(0, min);
                max = Math.max(0, max);
            }
            return { min, max };
        } catch (e) {
            return null;
        }
    }

    function _getCurrentWindowByValue(axis) {
        // axis: 'x' | 'y'，优先读取 dataZoom 的 startValue/endValue；否则根据 start/end + extent 推导
        const opt = chartInstance.getOption ? chartInstance.getOption() : null;
        const dzArr = (opt && opt.dataZoom) ? opt.dataZoom : [];
        const dz = dzArr[axis === 'x' ? config.xDataZoomIndex : config.yDataZoomIndex] || null;
        const ext = _getAxisExtent(axis);

        if (dz && dz.startValue != null && dz.endValue != null) {
            const s = Number(dz.startValue);
            const e = Number(dz.endValue);
            if (Number.isFinite(s) && Number.isFinite(e) && s !== e) {
                return { startValue: Math.min(s, e), endValue: Math.max(s, e), extent: ext };
            }
        }

        // fallback：从 percent 推导
        const start = (dz && typeof dz.start === 'number') ? dz.start : 0;
        const end = (dz && typeof dz.end === 'number') ? dz.end : 100;
        if (!ext) return null;
        const sv = ext.min + (ext.max - ext.min) * (start / 100);
        const ev = ext.min + (ext.max - ext.min) * (end / 100);
        return { startValue: Math.min(sv, ev), endValue: Math.max(sv, ev), extent: ext };
    }

    function _axisValueAtPixel(px, py) {
        // 返回 [xValue, yValue]，可能为 null
        try {
            const grid = _getGridRect();
            let x = px, y = py;
            if (grid) {
                // 压到绘图区内部，确保 convertFromPixel 稳定返回
                x = _clamp(x, grid.x + 1, grid.x + grid.width - 1);
                y = _clamp(y, grid.y + 1, grid.y + grid.height - 1);
            }
            const v = chartInstance.convertFromPixel({ gridIndex: config.gridIndex }, [x, y]);
            if (!v || !Array.isArray(v) || v.length < 2) return null;
            const xv = Number(v[0]);
            const yv = Number(v[1]);
            return [Number.isFinite(xv) ? xv : null, Number.isFinite(yv) ? yv : null];
        } catch (e) {
            return null;
        }
    }

    function _applyDataZoomByValue(axis, startValue, endValue) {
        const s = Number(startValue);
        const e = Number(endValue);
        if (!Number.isFinite(s) || !Number.isFinite(e) || s === e) return;
        const idx = (axis === 'x') ? config.xDataZoomIndex : config.yDataZoomIndex;
        chartInstance.dispatchAction({
            type: 'dataZoom',
            dataZoomIndex: idx,
            startValue: Math.min(s, e),
            endValue: Math.max(s, e)
        });
    }

    function _zoomAroundAnchor(axis, anchorValue, factor) {
        const win = _getCurrentWindowByValue(axis);
        if (!win) return;
        const s0 = win.startValue;
        const e0 = win.endValue;
        const a = Number(anchorValue);
        const range0 = Math.max(1e-9, e0 - s0);

        // 如果拿不到锚点，就用窗口中心缩放
        const anchor = Number.isFinite(a) ? a : (s0 + e0) / 2;
        const ratio = _clamp((anchor - s0) / range0, 0, 1);
        const range1 = Math.max(1e-9, range0 / factor);
        let ns = anchor - ratio * range1;
        let ne = ns + range1;

        // clamp 到全量范围（extent）
        const ext = win.extent || _getAxisExtent(axis);
        if (ext && Number.isFinite(ext.min) && Number.isFinite(ext.max) && ext.max > ext.min) {
            const full = ext.max - ext.min;
            // 关键修复：当范围已经“非常接近全量范围”时，直接吸附回全量范围
            // 否则会出现用户滚轮缩小很多次仍差一点回不去（体验上就是“卡住”）
            if (range1 >= full * 0.98) {
                ns = ext.min;
                ne = ext.max;
            } else {
                if (ns < ext.min) {
                    const off = ext.min - ns;
                    ns = ext.min;
                    ne += off;
                }
                if (ne > ext.max) {
                    const off = ne - ext.max;
                    ne = ext.max;
                    ns -= off;
                }
                // 二次保护
                ns = Math.max(ext.min, ns);
                ne = Math.min(ext.max, ne);
            }
        }

        _applyDataZoomByValue(axis, ns, ne);
    }

    // Grab 拖拽状态（用 value window 平移，稳定）
    let dragging = false;
    let dragStartPx = null;  // {x,y}（zr 像素）
    let dragStartWin = null; // {x0,x1,y0,y1}

    function _startDrag(px, py) {
        const region = _hitRegion(px, py);
        if (region !== 'plot') return;
        const wx = _getCurrentWindowByValue('x');
        const wy = _getCurrentWindowByValue('y');
        if (!wx || !wy) return;
        dragging = true;
        dragStartPx = { x: px, y: py };
        dragStartWin = { x0: wx.startValue, x1: wx.endValue, y0: wy.startValue, y1: wy.endValue };
        chartDom.style.cursor = 'grabbing';
    }

    function _moveDrag(px, py) {
        if (!dragging || !dragStartPx || !dragStartWin) return;
        const grid = _getGridRect();
        const w = grid ? grid.width : Math.max(1, chartDom.getBoundingClientRect().width);
        const h = grid ? grid.height : Math.max(1, chartDom.getBoundingClientRect().height);

        const dxPx = px - dragStartPx.x;
        const dyPx = py - dragStartPx.y;

        const xRange = Math.max(1e-9, dragStartWin.x1 - dragStartWin.x0);
        const yRange = Math.max(1e-9, dragStartWin.y1 - dragStartWin.y0);

        // 关键：只做平移，不改变窗口跨度
        // Grab 规则：右拖 -> 波形右移 -> 窗口左移；上拖 -> 波形上移 -> 窗口下移
        // 像素 -> 值域换算：dxPx/w * xRange
        const xShiftVal = -(dxPx / Math.max(1, w)) * xRange * config.panSpeed;
        // 关键：Y 方向按“Grab 模式”对齐项目规则
        // - 鼠标下拖(dy>0) -> 波形下移 -> 窗口上移（y 值增大）
        // - 鼠标上拖(dy<0) -> 波形上移 -> 窗口下移（y 值减小）
        // 因此这里不取反号，让 yShiftVal 与 dy 同号即可。
        const yShiftVal = (dyPx / Math.max(1, h)) * yRange * config.panSpeed;

        _applyDataZoomByValue('x', dragStartWin.x0 + xShiftVal, dragStartWin.x1 + xShiftVal);
        _applyDataZoomByValue('y', dragStartWin.y0 + yShiftVal, dragStartWin.y1 + yShiftVal);
    }

    function _endDrag() {
        if (!dragging) return;
        dragging = false;
        dragStartPx = null;
        dragStartWin = null;
        chartDom.style.cursor = '';
    }

    // 事件：滚轮缩放（轴独立/图内同时）
    const onWheel = (params) => {
        const ev = params && params.event ? params.event : null;
        if (ev && typeof ev.preventDefault === 'function') ev.preventDefault();

        const px = params.offsetX;
        const py = params.offsetY;
        if (!Number.isFinite(px) || !Number.isFinite(py)) return;

        const region = _hitRegion(px, py);
        if (region === 'none') return;

        const delta = (typeof params.wheelDelta === 'number') ? params.wheelDelta : (typeof ev?.deltaY === 'number' ? -ev.deltaY : 0);
        const factor = (delta > 0) ? config.zoomInFactor : config.zoomOutFactor;

        const v = _axisValueAtPixel(px, py);
        const anchorX = v ? v[0] : null;
        const anchorY = v ? v[1] : null;

        if (region === 'x') {
            _zoomAroundAnchor('x', anchorX, factor);
            return;
        }
        if (region === 'y') {
            _zoomAroundAnchor('y', anchorY, factor);
            return;
        }
        // plot：同时缩放
        _zoomAroundAnchor('x', anchorX, factor);
        _zoomAroundAnchor('y', anchorY, factor);
    };

    // 双击重置：双击 X 轴重置 X；双击 Y 轴重置 Y；双击绘图区重置两者
    const onDblClick = (params) => {
        const px = params.offsetX;
        const py = params.offsetY;
        if (!Number.isFinite(px) || !Number.isFinite(py)) return;
        const region = _hitRegion(px, py);
        const opt = chartInstance.getOption ? chartInstance.getOption() : null;
        const dzArr = (opt && opt.dataZoom) ? opt.dataZoom : [];

        function _resetOne(idx) {
            if (!dzArr[idx]) return;
            chartInstance.dispatchAction({ type: 'dataZoom', dataZoomIndex: idx, start: 0, end: 100 });
        }

        if (region === 'x') {
            _resetOne(config.xDataZoomIndex);
        } else if (region === 'y') {
            _resetOne(config.yDataZoomIndex);
        } else if (region === 'plot') {
            _resetOne(config.xDataZoomIndex);
            _resetOne(config.yDataZoomIndex);
        }
    };

    const onDown = (params) => {
        const ev = params && params.event ? params.event : null;
        // 仅左键
        if (ev && ev.button !== undefined && ev.button !== 0) return;
        const px = params.offsetX;
        const py = params.offsetY;
        if (!Number.isFinite(px) || !Number.isFinite(py)) return;
        _startDrag(px, py);
    };

    const onMove = (params) => {
        if (!dragging) return;
        const px = params.offsetX;
        const py = params.offsetY;
        if (!Number.isFinite(px) || !Number.isFinite(py)) return;
        _moveDrag(px, py);
    };

    const onUp = () => _endDrag();

    zr.on('mousewheel', onWheel);
    zr.on('dblclick', onDblClick);
    zr.on('mousedown', onDown);
    zr.on('mousemove', onMove);
    zr.on('mouseup', onUp);
    zr.on('globalout', onUp);

    chartInstance.__edgewindZoomHandlers = { onWheel, onDblClick, onDown, onMove, onUp };
}

/**
 * 重置图表缩放
 * @param {echarts.ECharts} chartInstance - ECharts图表实例
 */
function resetChartZoom(chartInstance) {
    if (!chartInstance) return;
    
    const currentOption = chartInstance.getOption();
    const currentDataZoom = currentOption.dataZoom || [];
    
    // 重置所有inside类型的dataZoom到全范围（0-100%）
    const newDataZoom = currentDataZoom.map(dz => {
        if (dz.type === 'inside') {
            return {
                ...dz,
                start: 0,
                end: 100
            };
        }
        return dz;
    });
    
    // 应用重置
    chartInstance.setOption({
        dataZoom: newDataZoom
    }, {
        notMerge: false,
        lazyUpdate: false
    });
}

