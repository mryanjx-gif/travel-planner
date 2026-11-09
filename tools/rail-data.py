#!/usr/bin/env python3
# 开发用脚本（1.3.0，不进安装包）：从 Overpass 一次性取默认清单 21 座城市的铁路线路几何，
# 简化后写进 assets/rail/（随源码提交，编译时打包成资源 :/rail/）。软件运行时不调 Overpass。
#
# 用法：python tools/rail-data.py [--raw 原始数据缓存目录] [--only sydney,tokyo] [--tol 10]
#   - 原始 JSON 存在 --raw（默认系统临时目录下 tp-rail-raw），已经取过的城市不再请求，可反复调简化参数
#   - 礼貌：串行、每座城市之间隔 10 秒、普通 User-Agent、失败最多重试 7 次（间隔 20 秒到 5 分钟，轮流换节点）
#
# 数据 © OpenStreetMap 贡献者，ODbL（软件"关于"里写了署名）。
#
# 输出格式（每城一个 <id>.txt，UTF-8 文本，资源里再由 rcc 压缩）：
#   每行一段轨道，Google encoded polyline（精度 1e-5，和 src/map/roads.cpp decodePolyline 一样）。
#   行首是 '#' 的是换乘连接（不同线路网之间 ≤250 米的最近点，算路时加罚）。
#   两段轨道在坐标（1e-5 取整后）相同的点上相连。
#   cities.txt：每行 "id 南 西 北 东"（度），算路时两站都落在同一座城市的框里才查。
import argparse
import json
import math
import os
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from collections import defaultdict

CITIES = [  # id, 中文名, 南, 西, 北, 东
    ("sydney", "悉尼", -34.20, 150.60, -33.55, 151.35),
    ("melbourne", "墨尔本", -38.30, 144.55, -37.55, 145.40),
    ("brisbane", "布里斯班", -27.75, 152.75, -27.10, 153.25),
    ("tokyo", "东京", 35.45, 139.35, 35.85, 140.00),
    ("osaka", "大阪", 34.50, 135.30, 34.85, 135.65),
    ("kyoto", "京都", 34.88, 135.65, 35.10, 135.85),
    ("seoul", "首尔", 37.40, 126.40, 37.70, 127.20),
    ("hongkong", "香港", 22.15, 113.82, 22.57, 114.32),
    ("taipei", "台北", 24.95, 121.15, 25.20, 121.65),
    ("singapore", "新加坡", 1.20, 103.60, 1.48, 104.05),
    ("bangkok", "曼谷", 13.55, 100.35, 14.05, 100.85),
    ("kualalumpur", "吉隆坡", 2.70, 101.45, 3.30, 101.85),
    ("london", "伦敦", 51.25, -0.55, 51.72, 0.30),
    ("paris", "巴黎", 48.65, 2.10, 49.05, 2.65),
    ("berlin", "柏林", 52.33, 13.08, 52.68, 13.77),
    ("rome", "罗马", 41.75, 12.20, 42.05, 12.70),
    ("newyork", "纽约", 40.50, -74.30, 40.95, -73.70),
    ("beijing", "北京", 39.45, 116.10, 40.15, 116.75),
    ("shanghai", "上海", 30.90, 121.10, 31.45, 121.90),
    ("guangzhou", "广州", 22.90, 113.10, 23.45, 113.60),
    ("shenzhen", "深圳", 22.45, 113.75, 22.85, 114.35),
]

# overpass-api.de 主地址在这台电脑上连不上；轮流用同一运营方的两个节点和一个公共镜像（遇到 502 / 504 换下一个）
ENDPOINTS = ["https://lz4.overpass-api.de/api/interpreter", "https://z.overpass-api.de/api/interpreter",
             "https://overpass.private.coffee/api/interpreter"]
UA = "TravelPlanner-dev-tools/1.3 (one-off data build for a free desktop app)"
KINDS = "rail|subway|light_rail|tram|monorail"
TRANSFER_M = 250


def query(s, w, n, e):
    bb = f"{s},{w},{n},{e}"
    return (f'[out:json][timeout:600][maxsize:536870912];('
            f'way["railway"~"^({KINDS})$"][!"service"]({bb});'
            f'way["railway"~"^({KINDS})$"]["service"="crossover"]({bb});'
            f');out skel geom;')


def fetch(cid, bbox, raw_dir):
    path = os.path.join(raw_dir, cid + ".json")
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path, False
    data = urllib.parse.urlencode({"data": query(*bbox)}).encode()
    for attempt, wait in enumerate([20, 40, 60, 90, 120, 180, 300, None]):
        try:
            req = urllib.request.Request(ENDPOINTS[attempt % len(ENDPOINTS)], data=data, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=900) as r:
                body = r.read()
            json.loads(body)   # 确认是完整的 JSON
            with open(path, "wb") as f:
                f.write(body)
            return path, True
        except Exception as ex:   # noqa: BLE001 —— 开发脚本，失败就重试 / 报告
            print(f"  {cid} 第 {attempt + 1} 次失败：{ex}", file=sys.stderr)
            if wait is None:
                raise
            time.sleep(wait)


def meters(a, b):
    k = math.cos(math.radians((a[0] + b[0]) / 2))
    return math.hypot(a[0] - b[0], (a[1] - b[1]) * k) * 111320


def dp(pts, tol_m):
    """Douglas-Peucker（保留首尾），按局部等距投影量米。"""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    k = math.cos(math.radians(pts[0][0])) * 111320
    while stack:
        i, j = stack.pop()
        ax, ay = pts[i][1] * k, pts[i][0] * 111320
        bx, by = pts[j][1] * k, pts[j][0] * 111320
        dx, dy = bx - ax, by - ay
        L2 = dx * dx + dy * dy
        best, bi = -1.0, -1
        for m in range(i + 1, j):
            px, py = pts[m][1] * k, pts[m][0] * 111320
            if L2 == 0:
                d = math.hypot(px - ax, py - ay)
            else:
                t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
                d = math.hypot(px - ax - t * dx, py - ay - t * dy)
            if d > best:
                best, bi = d, m
        if best > tol_m:
            keep[bi] = True
            stack += [(i, bi), (bi, j)]
    return [p for p, f in zip(pts, keep) if f]


def enc_num(v):
    v = ~(v << 1) if v < 0 else (v << 1)
    out = []
    while v >= 0x20:
        out.append(chr((0x20 | (v & 0x1f)) + 63))
        v >>= 5
    out.append(chr(v + 63))
    return "".join(out)


def encode(ipts):
    out, plat, plng = [], 0, 0
    for lat, lng in ipts:
        out.append(enc_num(lat - plat) + enc_num(lng - plng))
        plat, plng = lat, lng
    return "".join(out)


def build(raw_path, bbox, tol_m):
    s, w, n, e = bbox
    mg = 0.02
    inside = lambda p: s - mg <= p[0] <= n + mg and w - mg <= p[1] <= e + mg   # noqa: E731
    with open(raw_path, encoding="utf-8") as f:
        els = json.load(f)["elements"]
    ways = []
    for el in els:
        g = el.get("geometry")
        if el.get("type") == "way" and g:
            ways.append([(p["lat"], p["lon"]) if p else None for p in g])
    cnt = defaultdict(int)
    for wy in ways:
        for p in wy:
            if p:
                cnt[p] += 1
    # 在交汇点（被多条 way 用到的点）和框外切开，每段单独简化，交汇点一定保留
    pieces = []
    for wy in ways:
        cur = []
        for idx, p in enumerate(wy):
            if p is None or not inside(p):
                if len(cur) >= 2:
                    pieces.append(cur)
                cur = []
                continue
            cur.append(p)
            if cnt[p] > 1 and len(cur) >= 2 and idx != len(wy) - 1:
                pieces.append(cur)
                cur = [p]
        if len(cur) >= 2:
            pieces.append(cur)
    lines = []
    for pc in pieces:
        ip = []
        for lat, lng in dp(pc, tol_m):
            q = (round(lat * 1e5), round(lng * 1e5))
            if not ip or ip[-1] != q:
                ip.append(q)
        if len(ip) >= 2:
            lines.append(ip)
    # 合并首尾相接、中间点只被这两段用到的段（少写重复的端点）
    lines = merge_chains(lines)
    # 线路网（连通分量）：并查集
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for ln in lines:
        for a, b in zip(ln, ln[1:]):
            ra, rb = find(a), find(b)
            if ra != rb:
                parent[ra] = rb
    verts = {p for ln in lines for p in ln}
    comp = {p: find(p) for p in verts}
    # 换乘连接：不同分量之间 ≤250 米的最近点，每个 300 米格子 + 分量对只留一条
    grid = defaultdict(list)
    for p in verts:
        grid[(p[0] // 300, p[1] // 300)].append(p)   # 1e-5 度单位，300 = 0.003 度
    best = {}
    for p in verts:
        cx, cy = p[0] // 300, p[1] // 300
        cp = comp[p]
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for q in grid.get((cx + dx, cy + dy), ()):
                    cq = comp[q]
                    if cq == cp:
                        continue
                    d = meters((p[0] / 1e5, p[1] / 1e5), (q[0] / 1e5, q[1] / 1e5))
                    if d > TRANSFER_M:
                        continue
                    key = (min(cp, cq), max(cp, cq), cx, cy)
                    if key not in best or d < best[key][0]:
                        best[key] = (d, p, q)
    seen = set()
    links = []
    for d, p, q in best.values():
        k = (min(p, q), max(p, q))
        if k not in seen:
            seen.add(k)
            links.append([p, q])
    text = "\n".join(encode(ln) for ln in lines) + "\n" + "".join("#" + encode(ln) + "\n" for ln in links)
    return text, len(lines), sum(len(ln) for ln in lines), len(links), len(set(comp.values()))


def merge_chains(lines):
    deg = defaultdict(int)
    for ln in lines:
        deg[ln[0]] += 1
        deg[ln[-1]] += 1
    # 内部点也算度（段在别人中间接上来时不能合并）
    inner = defaultdict(int)
    for ln in lines:
        for p in ln[1:-1]:
            inner[p] += 1
    ends = defaultdict(list)
    for i, ln in enumerate(lines):
        ends[ln[0]].append(i)
        ends[ln[-1]].append(i)
    used = [False] * len(lines)
    out = []
    for i in range(len(lines)):
        if used[i]:
            continue
        used[i] = True
        ln = list(lines[i])
        for direction in (0, 1):
            while True:
                tip = ln[-1]
                if deg[tip] != 2 or inner[tip]:
                    break
                nxt = [j for j in ends[tip] if not used[j]]
                if not nxt:
                    break
                j = nxt[0]
                used[j] = True
                seg = lines[j] if lines[j][0] == tip else lines[j][::-1]
                ln += seg[1:]
            ln.reverse()
        out.append(ln)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=os.path.join(tempfile.gettempdir(), "tp-rail-raw"))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "rail"))
    ap.add_argument("--only", default="")
    ap.add_argument("--tol", type=float, default=10.0, help="简化容差（米）")
    a = ap.parse_args()
    os.makedirs(a.raw, exist_ok=True)
    os.makedirs(a.out, exist_ok=True)
    only = set(filter(None, a.only.split(",")))
    total = 0
    first = True
    for cid, name, *bbox in CITIES:
        if only and cid not in only:
            continue
        raw = os.path.join(a.raw, cid + ".json")
        if not (os.path.exists(raw) and os.path.getsize(raw) > 0):
            if not first:
                time.sleep(10)
            first = False
            print(f"取 {name}（{cid}）…", flush=True)
        try:
            raw, _ = fetch(cid, bbox, a.raw)
        except Exception as ex:   # noqa: BLE001
            print(f"  {name} 取不到，跳过：{ex}", file=sys.stderr)
            continue
        text, nl, nv, nk, nc = build(raw, bbox, a.tol)
        out = os.path.join(a.out, cid + ".txt")
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        size = os.path.getsize(out)
        total += size
        print(f"  {name}：{nl} 段、{nv} 点、{nk} 条换乘连接、{nc} 个线路网，{size / 1024:.0f} KB", flush=True)
    with open(os.path.join(a.out, "cities.txt"), "w", encoding="utf-8", newline="\n") as f:
        for cid, name, s, w, n, e in CITIES:
            if os.path.exists(os.path.join(a.out, cid + ".txt")):
                f.write(f"{cid} {s} {w} {n} {e}\n")
    print(f"合计 {total / 1024 / 1024:.2f} MB（未压缩）")


if __name__ == "__main__":
    main()
