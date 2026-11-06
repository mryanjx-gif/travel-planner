# 把脚本转成 Windows 需要的格式：.bat → CRLF、无 BOM；.ps1 → CRLF、UTF-8 BOM（PowerShell 5.1 才能认中文）
import sys
for p in sys.argv[1:]:
    b = open(p, 'rb').read()
    if b.startswith(b'\xef\xbb\xbf'): b = b[3:]
    b = b.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    if p.endswith('.ps1'): b = b'\xef\xbb\xbf' + b
    open(p, 'wb').write(b)
    print('ok', p)
