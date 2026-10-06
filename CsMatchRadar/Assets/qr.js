// 二维码渲染（供 WebView2 加载）。
//
// 二维码编码本身使用 vendored 的 qrcode.min.js
// （qrcode-generator, Kazuhiko Arase, MIT 许可），
// 本文件只负责：调用它生成模块矩阵 → 画到 canvas。
//
// 为什么不用自己写编码器：我先手写实现过一版，用参考实现逐格比对时
// 发现格式信息布局与掩码选择都有偏差（掩码必须按标准试 8 种取惩罚分最低者）。
// 二维码编码细节多、出错后仍可能"看起来像二维码"，排查成本高，
// 因此直接采用成熟实现，本文件保持极薄。
//
// 宿主导入方式：WebView2 里调用 window.renderQr(url)，返回状态字符串。

(function () {
    'use strict';

    // 渲染尺寸（CSS 像素）。二维码本身用白底黑块，保证深色主题下可扫。
    var TARGET_SIZE = 240;

    window.renderQr = function (text) {
        var root = document.getElementById('qr');
        try {
            if (!text) {
                throw new Error('内容为空');
            }
            if (typeof qrcode !== 'function') {
                throw new Error('qrcode 库未加载');
            }

            // typeNumber 0 = 自动选择最小可用版本；
            // 纠错等级 L 可容纳更长内容（这里是 URL，长度约 80 字符）。
            var qr = qrcode(0, 'L');
            qr.addData(text);
            qr.make();

            var count = qr.getModuleCount();
            var quiet = 4;                       // 静默区（标准要求 4 模块）
            var total = count + quiet * 2;
            var scale = Math.max(2, Math.floor(TARGET_SIZE / total));
            var dim = total * scale;

            var canvas = document.createElement('canvas');
            canvas.width = dim;
            canvas.height = dim;
            var ctx = canvas.getContext('2d');

            ctx.fillStyle = '#ffffff';
            ctx.fillRect(0, 0, dim, dim);
            ctx.fillStyle = '#000000';
            for (var r = 0; r < count; r++) {
                for (var c = 0; c < count; c++) {
                    if (qr.isDark(r, c)) {
                        ctx.fillRect((c + quiet) * scale,
                                     (r + quiet) * scale,
                                     scale, scale);
                    }
                }
            }

            root.innerHTML = '';
            root.appendChild(canvas);
            return 'ok';
        } catch (e) {
            root.innerHTML = '<div class="err">二维码生成失败：' +
                (e && e.message ? e.message : String(e)) + '</div>';
            return 'error: ' + (e && e.message ? e.message : String(e));
        }
    };
})();
