# 把 maplibre-native-qt 工作副本（含子模块）打成 zip：不含 .git；符号链接按目标内容存；固定顺序和时间戳（可复现）
import os, sys, zipfile, hashlib, json
src, out, listfile = sys.argv[1:4]
PREFIX = len(r"D:\旅行计划Native\third_party\maplibre-native-qt\src" + "\\")
entries, skipped_links = [], []
for root, dirs, files in os.walk(src):
    dirs[:] = sorted(d for d in dirs if d != '.git')
    for f in sorted(files):
        if f == '.git':
            continue
        p = os.path.join(root, f)
        rel = os.path.relpath(p, src).replace(os.sep, '/')
        if os.path.islink(p) and not os.path.exists(p):
            skipped_links.append(rel); continue
        entries.append(rel)
entries.sort()
longs = []
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel in entries:
        p = os.path.join(src, rel)
        zi = zipfile.ZipInfo(rel, date_time=(2026, 11, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        mode = os.stat(p).st_mode
        zi.external_attr = (mode & 0o777 | 0o100000) << 16
        with open(p, 'rb') as fh:
            z.writestr(zi, fh.read(), compresslevel=9)
        full = PREFIX + len(rel)
        d = PREFIX + len(rel.rsplit('/', 1)[0]) if '/' in rel else PREFIX
        if full > 259 or d > 247:
            longs.append(rel)
with open(listfile, 'w', encoding='utf-8') as fh:
    json.dump({"files": len(entries), "dangling_links": skipped_links, "long_paths": len(longs), "long_path_list": longs}, fh, ensure_ascii=False)
print(len(entries), 'files;', len(longs), 'long;', 'dangling:', skipped_links)
