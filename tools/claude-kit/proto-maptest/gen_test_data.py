"""阶段 ⓪ 地图测试数据生成（只读输入，输出到 out_dir）。

用法：python3 -I gen_test_data.py <data.js 副本> <Electron 的 admin.js> <out_dir>
生成：routes.geojson、stops.geojson、adm0.geojson、style.json（路线样式沿用 Electron 的 LINE_STYLE / DASH_PX）。
注意：生成的 GeoJSON 含个人行程坐标，只放在云端临时目录，不写回项目。
"""
import json, os, sys

data_js, admin_js, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
src = open(data_js, encoding='utf-8').read()
mark = 'window.TRAVEL_DATA = '
D = json.loads(src[src.index(mark) + len(mark):-2])
dash = {'planning': 'dash', 'wish': 'dot', 'completed': 'solid'}
pts, lines = [], []
for p in D['plans']:
    stops = sorted([s for s in p.get('stops', []) if isinstance(s.get('lat'), (int, float))],
                   key=lambda s: s.get('order', 0))
    col = p.get('color') or '#2d5f8b'
    for k, s in enumerate(stops):
        pts.append({"type": "Feature", "geometry": {"type": "Point", "coordinates": [s['lng'], s['lat']]},
                    "properties": {"color": s.get('color') or col, "n": k + 1}})
    for a, b in zip(stops, stops[1:]):
        lines.append({"type": "Feature",
                      "geometry": {"type": "LineString", "coordinates": [[a['lng'], a['lat']], [b['lng'], b['lat']]]},
                      "properties": {"color": a.get('color') or col, "dash": dash.get(p.get('status'), 'solid')}})
json.dump({"type": "FeatureCollection", "features": lines}, open(f'{out}/routes.geojson', 'w'), ensure_ascii=False)
json.dump({"type": "FeatureCollection", "features": pts}, open(f'{out}/stops.geojson', 'w'), ensure_ascii=False)
adm = open(admin_js, encoding='utf-8').read()
A = json.loads(adm[adm.index('{'):adm.rindex('}') + 1])
json.dump(A['adm0'], open(f'{out}/adm0.geojson', 'w'))

W = 3.5  # LINE_STYLE.idle.weight
def route(id_, kind, px=None):
    paint = {"line-color": ["get", "color"], "line-width": W, "line-opacity": 0.92}
    if px:
        paint["line-dasharray"] = [v / W for v in px]  # DASH_PX 是像素，dasharray 单位是线宽
    return {"id": id_, "type": "line", "source": "routes", "filter": ["==", ["get", "dash"], kind],
            "layout": {"line-join": "round", "line-cap": "round"}, "paint": paint}
o = os.path.abspath(out)
style = {"version": 8, "name": "tp-test",
         "sources": {k: {"type": "geojson", "data": f"file://{o}/{k}.geojson"} for k in ("adm0", "routes", "stops")},
         "layers": [
             {"id": "bg", "type": "background", "paint": {"background-color": "#d9e4ea"}},
             {"id": "u-adm0", "type": "line", "source": "adm0",
              "paint": {"line-color": "#7d746a", "line-width": ["interpolate", ["linear"], ["zoom"], 1, 0.6, 5, 1.4]}},
             {"id": "rt-edge", "type": "line", "source": "routes", "layout": {"line-join": "round", "line-cap": "round"},
              "paint": {"line-color": "#ffffff", "line-width": 6.5, "line-opacity": 0.85}},
             route("rt-solid", "solid"), route("rt-dash", "dash", [4, 8]), route("rt-dot", "dot", [0.5, 7]),
             {"id": "stops", "type": "circle", "source": "stops",
              "paint": {"circle-radius": 4.5, "circle-color": ["get", "color"],
                        "circle-stroke-color": "#ffffff", "circle-stroke-width": 1.5}}]}
json.dump(style, open(f'{out}/style.json', 'w'), ensure_ascii=False, indent=1)
print(len(D['plans']), 'plans', len(pts), 'stops', len(lines), 'legs')
