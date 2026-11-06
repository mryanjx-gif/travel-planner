# 云端测试用的假 Esri 瓦片服务（0.8.5）：按 /<服务>/MapServer/tile/<z>/<y>/<x> 画一张带编号的格子 PNG。
# 用法：python3 fake-upstream.py 8765，然后程序加环境变量 TP_TILE_UPSTREAM=http://127.0.0.1:8765（只在开发版认）
import http.server, io, re, sys
from PIL import Image, ImageDraw
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        m = re.search(r'/(\w+)/MapServer/tile/(\d+)/(\d+)/(\d+)', self.path)
        if not m: self.send_response(404); self.end_headers(); return
        name, z, y, x = m.group(1), *map(int, m.groups()[1:])
        col = {'World_Street_Map': (236, 214, 170), 'World_Light_Gray_Base': (210, 210, 210), 'World_Imagery': (40, 70, 50)}.get(name, (200, 100, 100))
        im = Image.new('RGB', (256, 256), col)
        d = ImageDraw.Draw(im); d.rectangle([0, 0, 255, 255], outline=(120, 90, 60), width=2); d.text((10, 10), f'{z}/{y}/{x}', fill=(60, 40, 20))
        b = io.BytesIO(); im.save(b, 'PNG'); data = b.getvalue()
        self.send_response(200); self.send_header('Content-Type', 'image/png'); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
