#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_trace.py —— 把 sim_demo.exe 产出的遥测 CSV 变成可写进报告的工程数据

为什么需要它：
  "曲线看起来还行"不是工程结论。答辩和调参记录需要的是可比较的量化指标：
  超调、上升时间、整定时间、稳态误差。这些必须由脚本从原始数据算出来，
  而不是靠肉眼从图上读 —— 肉眼读数不可复现，也无法回归对比。

用法：
    python tools/analyze_trace.py docs/data/trace_normal.csv --svg docs/data/step_response.svg
输出：
    标准输出打印 Markdown 表格；--svg 指定时同时输出矢量图。
"""

import argparse
import csv
import os
import sys


def load(path):
    rows = []
    with open(path, newline='', encoding='utf-8') as fh:
        for row in csv.DictReader(fh):
            try:
                rows.append({
                    't':      float(row['tick_ms']),
                    'target': float(row['target_speed']),
                    'actual': float(row['actual_speed']),
                    'output': float(row['control_output']),
                    'pos':    float(row['position']),
                    'state':  row['state'].strip(),
                    'faults': int(row['faults'], 16),
                })
            except (KeyError, ValueError):
                continue
    return rows


def split_steps(rows, tol=1e-3):
    """按 target 变化切分为若干阶跃段，返回 [(起始下标, 结束下标)]

    同时也在 state 变化处切分：否则一段 -800 的阶跃会被后面的 STOP 污染 ——
    停机会让 actual 自行衰减，算出来的稳态误差 +284 完全是假的。
    数据里混进一个假指标，比没有指标更危险。
    只有整段处于 RUNNING 的区间才是有意义的速度环阶跃响应。
    """
    if not rows:
        return []
    bounds = [0]
    for i in range(1, len(rows)):
        if abs(rows[i]['target'] - rows[i - 1]['target']) > tol:
            bounds.append(i)
        elif rows[i]['state'] != rows[i - 1]['state']:
            bounds.append(i)
    bounds.append(len(rows))

    segs = []
    for i in range(len(bounds) - 1):
        a, b = bounds[i], bounds[i + 1]
        if b - a >= 5 and all(r['state'] == 'RUNNING' for r in rows[a:b]):
            segs.append((a, b))
    return segs


def metrics(seg, settle_band=0.02):
    """计算一个阶跃段的量化指标"""
    t = [r['t'] for r in seg]
    y = [r['actual'] for r in seg]
    sp = seg[-1]['target']

    # 段起点作为初值（切换瞬间之前的值更准确，但相邻段已足够）
    y0 = y[0]
    span = sp - y0
    if abs(span) < 1e-9:
        return None

    # 超调：相对阶跃幅度
    if span > 0:
        peak = max(y)
        overshoot = (peak - sp) / span * 100.0
    else:
        peak = min(y)
        overshoot = (sp - peak) / (-span) * 100.0
    overshoot = max(overshoot, 0.0)

    # 上升时间 10% -> 90%
    def cross(frac):
        level = y0 + span * frac
        for i, v in enumerate(y):
            if (span > 0 and v >= level) or (span < 0 and v <= level):
                return t[i]
        return None

    t10, t90 = cross(0.10), cross(0.90)
    rise = (t90 - t10) if (t10 is not None and t90 is not None) else None

    # 整定时间：之后一直保持在 ±band 之内
    band = abs(span) * settle_band
    settle = None
    for i in range(len(y) - 1, -1, -1):
        if abs(y[i] - sp) > band:
            settle = (t[i + 1] - t[0]) if i + 1 < len(t) else None
            break
    if settle is None:
        settle = t[-1] - t[0]

    # 稳态误差：取段尾 20% 的平均值
    tail = y[max(0, int(len(y) * 0.8)):]
    sse = (sum(tail) / len(tail)) - sp

    return {
        't_start': t[0], 'from': y0, 'to': sp,
        'overshoot': overshoot, 'peak': peak,
        'rise': rise, 'settle': settle, 'sse': sse,
        'state': seg[-1]['state'],
    }


def svg_plot(rows, path, title):
    """输出一张无依赖的双面板 SVG（阶跃响应 + 控制输出）"""
    W, H = 1000, 620
    PAD_L, PAD_R, PAD_T = 70, 30, 50
    PANEL_H, GAP = 230, 60

    if not rows:
        return
    t0, t1 = rows[0]['t'], rows[-1]['t']
    ys = [r['actual'] for r in rows] + [r['target'] for r in rows]
    ymin, ymax = min(ys), max(ys)
    if ymax - ymin < 1e-6:
        ymax = ymin + 1.0
    pad = (ymax - ymin) * 0.1
    ymin -= pad
    ymax += pad

    us = [r['output'] for r in rows]
    umin, umax = min(us + [0.0]), max(us + [0.0])
    if umax - umin < 1e-6:
        umax = umin + 1.0
    upad = (umax - umin) * 0.1
    umin -= upad
    umax += upad

    def sx(t):
        return PAD_L + (t - t0) / max(t1 - t0, 1e-9) * (W - PAD_L - PAD_R)

    def sy(v):
        return PAD_T + (ymax - v) / (ymax - ymin) * PANEL_H

    def sy2(v):
        top = PAD_T + PANEL_H + GAP
        return top + (umax - v) / (umax - umin) * PANEL_H

    def poly(key, mapper):
        return ' '.join('%.2f,%.2f' % (sx(r['t']), mapper(r[key])) for r in rows)

    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
               'viewBox="0 0 %d %d" font-family="Consolas,monospace" font-size="13">' % (W, H, W, H))
    out.append('<rect width="%d" height="%d" fill="#ffffff"/>' % (W, H))
    out.append('<text x="%d" y="26" font-size="16">%s</text>' % (PAD_L, title))

    # 面板 1 网格与坐标
    out.append('<rect x="%d" y="%d" width="%d" height="%d" fill="#fafafa" stroke="#cccccc"/>'
               % (PAD_L, PAD_T, W - PAD_L - PAD_R, PANEL_H))
    for k in range(5):
        v = ymin + (ymax - ymin) * k / 4.0
        y = sy(v)
        out.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e8e8e8"/>'
                   % (PAD_L, y, W - PAD_R, y))
        out.append('<text x="%d" y="%.1f" fill="#666" text-anchor="end">%.0f</text>'
                   % (PAD_L - 8, y + 4, v))
    out.append('<polyline fill="none" stroke="#8888cc" stroke-width="1.6" '
               'stroke-dasharray="6,4" points="%s"/>' % poly('target', sy))
    out.append('<polyline fill="none" stroke="#cc2222" stroke-width="2" points="%s"/>'
               % poly('actual', sy))
    out.append('<text x="%d" y="%d" fill="#cc2222">actual_speed</text>' % (W - PAD_R - 130, PAD_T + 16))
    out.append('<text x="%d" y="%d" fill="#7777aa">target_speed (dashed)</text>' % (W - PAD_R - 190, PAD_T + 34))

    # 面板 2
    top2 = PAD_T + PANEL_H + GAP
    out.append('<rect x="%d" y="%d" width="%d" height="%d" fill="#fafafa" stroke="#cccccc"/>'
               % (PAD_L, top2, W - PAD_L - PAD_R, PANEL_H))
    for k in range(5):
        v = umin + (umax - umin) * k / 4.0
        y = sy2(v)
        out.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#e8e8e8"/>'
                   % (PAD_L, y, W - PAD_R, y))
        out.append('<text x="%d" y="%.1f" fill="#666" text-anchor="end">%.1f</text>'
                   % (PAD_L - 8, y + 4, v))
    out.append('<polyline fill="none" stroke="#116611" stroke-width="1.6" points="%s"/>'
               % poly('output', sy2))
    out.append('<text x="%d" y="%d" fill="#116611">control_output (PID -> VirtualMotor input)</text>'
               % (PAD_L + 8, top2 + 18))

    # 时间轴
    out.append('<text x="%d" y="%d" fill="#666">t / ms</text>' % (W - PAD_R - 60, H - 12))
    for k in range(6):
        t = t0 + (t1 - t0) * k / 5.0
        x = sx(t)
        out.append('<text x="%.1f" y="%d" fill="#666" text-anchor="middle">%.0f</text>'
                   % (x, H - 30, t))

    out.append('</svg>')
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(out))


def report(csv_path, lines, band):
    rows = load(csv_path)
    if not rows:
        lines.append('no data in %s' % csv_path)
        return rows

    lines.append('## %s' % os.path.basename(csv_path))
    lines.append('')
    lines.append('样本数 %d，时长 %.0f ms，末态 **%s**，故障位 0x%08X'
                 % (len(rows), rows[-1]['t'], rows[-1]['state'], rows[-1]['faults']))
    lines.append('')
    lines.append('| # | 起始 (ms) | 阶跃 | 超调 (%) | 峰值 | 上升时间 (ms) | 整定时间 (ms) | 稳态误差 |')
    lines.append('|---|-----------|------|----------|------|---------------|---------------|----------|')

    n = 0
    for (a, b) in split_steps(rows):
        m = metrics(rows[a:b], band)
        if m is None:
            continue
        n += 1
        rise = '--' if m['rise'] is None else '%.0f' % m['rise']
        lines.append('| %d | %.0f | %.0f -> %.0f | %.2f | %.1f | %s | %.0f | %+.3f |'
                     % (n, m['t_start'], m['from'], m['to'],
                        m['overshoot'], m['peak'], rise, m['settle'], m['sse']))
    if n == 0:
        lines.append('| - | - | （无完整 RUNNING 阶跃段） | - | - | - | - | - |')
    lines.append('')
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('csv', nargs='+', help='one or more telemetry CSV files')
    ap.add_argument('--svg', default=None, help='write an SVG plot for the FIRST csv')
    ap.add_argument('--md', default=None, help='write the Markdown report to this file (UTF-8)')
    ap.add_argument('--title', default='RoboControl V1.0 主机仿真测试数据')
    ap.add_argument('--band', type=float, default=0.02)
    args = ap.parse_args()

    lines = []
    if args.md:
        lines.append('# %s' % args.title)
        lines.append('')
        lines.append('由 tools/analyze_trace.py 从原始遥测 CSV 自动生成，请勿手工编辑。')
        lines.append('')
        lines.append('判定口径：超调与整定时间按阶跃幅度计算，整定带为 ±%.0f%%；'
                     '只统计整段处于 RUNNING 的区间。' % (args.band * 100.0))
        lines.append('')

    first_rows = []
    for idx, path in enumerate(args.csv):
        rows = report(path, lines, args.band)
        if idx == 0:
            first_rows = rows

    if args.svg and first_rows:
        svg_plot(first_rows, args.svg, args.title)
        lines.append('图：docs/data/%s' % os.path.basename(args.svg))
        lines.append('')

    text = '\n'.join(lines)
    if args.md:
        with open(args.md, 'w', encoding='utf-8') as fh:
            fh.write(text + '\n')
        print('report written to %s' % args.md)
    else:
        print(text)

    return 0


if __name__ == '__main__':
    sys.exit(main())
