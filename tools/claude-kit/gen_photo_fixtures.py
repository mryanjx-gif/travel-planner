#!/usr/bin/env python3
"""生成测试照片（0.7.0，EXIF / 缩略图 / 旋转测试用），打包成 tests/photos.txt。

需要：Pillow（带 WebP）、pillow-heif（只在云端生成 HEIC 时用）、exiftool（写 EXIF）。
用法：python3 gen_photo_fixtures.py <临时文件夹> <tests/photos.txt>
照片不直接放进项目：写回你电脑时图片会被加上 C2PA 内容凭证（实测 0.7.0，每张多约 5.8 KB），逐字节校验对不上；
所以打包成文本（每行"文件名<Tab>base64"），单元测试运行时解到临时文件夹。
结果是固定的（同样的像素、同样的 EXIF），重新生成后单元测试的期望值不用改。
照片内容：64×48，左上角红色、右上角绿色、左下角蓝色、右下角白色（判断方向用）。
"""
import os
import subprocess
import sys

from PIL import Image

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def quad(w=64, h=48, mode='RGB'):
    im = Image.new(mode, (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            left, top = x < w // 2, y < h // 2
            c = (255, 0, 0) if left and top else (0, 255, 0) if top else (0, 0, 255) if left else (255, 255, 255)
            px[x, y] = c + ((255,) if mode == 'RGBA' else ())
    return im


def et(path, *args):
    subprocess.run(['exiftool', '-q', '-overwrite_original', *args, path], check=True)


FULL = ['-DateTimeOriginal=2024:10:03 14:25:36', '-OffsetTimeOriginal=+09:00',
        '-GPSLatitude=35.658581', '-GPSLatitudeRef=N', '-GPSLongitude=139.745433', '-GPSLongitudeRef=E',
        '-GPSAltitude=40.5', '-GPSAltitudeRef=0', '-GPSImgDirection=214.5', '-GPSImgDirectionRef=T',
        '-Make=Apple', '-Model=iPhone 15 Pro', '-LensModel=iPhone 15 Pro back camera 6.86mm f/1.78',
        '-FNumber=1.78', '-ExposureTime=1/250', '-ISO=80', '-FocalLength=6.86', '-FocalLengthIn35mmFormat=24',
        '-ExposureCompensation=-0.7']

# JPEG：完整 EXIF，方向 6，小端
p = os.path.join(out, 'full_le.jpg'); quad().save(p, quality=90); et(p, *FULL, '-Orientation#=6', '-ExifByteOrder=II')
# JPEG：完整 EXIF，方向 6，大端
p = os.path.join(out, 'full_be.jpg'); quad().save(p, quality=90); et(p, *FULL, '-Orientation#=6', '-ExifByteOrder=MM')
# JPEG：没有 OffsetTime，时区靠 GPS 的 UTC 时间推算（14:25:36 当地 − 05:25:30 UTC = +09:00），南半球西经，海拔在海平面下
p = os.path.join(out, 'gps_tz.jpg'); quad().save(p, quality=90)
et(p, '-DateTimeOriginal=2024:10:03 14:25:36', '-GPSDateStamp=2024:10:03', '-GPSTimeStamp=05:25:30',
   '-GPSLatitude=33.8688', '-GPSLatitudeRef=S', '-GPSLongitude=70.5', '-GPSLongitudeRef=W', '-GPSAltitude=12', '-GPSAltitudeRef#=1',
   '-Orientation#=1')
# JPEG：没有 EXIF
p = os.path.join(out, 'noexif.jpg'); quad().save(p, quality=90)
# JPEG：有 EXIF 没有方向，带一张嵌入小图（IFD1）
p = os.path.join(out, 'noorient.jpg'); quad().save(p, quality=90)
thumb = os.path.join(out, '_thumb.jpg'); quad(16, 12).save(thumb, quality=80)
et(p, '-DateTimeOriginal=2023:05:01 08:00:00', '-Make=Canon', '-Model=Canon EOS R5', f'-ThumbnailImage<={thumb}')
os.remove(thumb)
# JPEG：方向 3，XMP 里也有 tiff:Orientation
p = os.path.join(out, 'xmp_orient.jpg'); quad().save(p, quality=90); et(p, '-Orientation#=3', '-XMP-tiff:Orientation#=3')
# JPEG：GPSStatus = V（定位无效）、0,0
p = os.path.join(out, 'gps_void.jpg'); quad().save(p, quality=90)
et(p, '-GPSLatitude=35', '-GPSLatitudeRef=N', '-GPSLongitude=139', '-GPSLongitudeRef=E', '-GPSStatus#=V')
p = os.path.join(out, 'gps_zero.jpg'); quad().save(p, quality=90)
et(p, '-GPSLatitude=0', '-GPSLatitudeRef=N', '-GPSLongitude=0', '-GPSLongitudeRef=E', '-DateTimeOriginal#=0000:00:00 00:00:00')
# PNG：eXIf 块，方向 8
p = os.path.join(out, 'exif.png'); quad().save(p); et(p, *FULL, '-Orientation#=8')
# PNG：带透明，没有 EXIF
p = os.path.join(out, 'alpha.png'); quad(mode='RGBA').save(p)
# WebP：有损 + EXIF（方向 6）；无损 + EXIF（方向 1）
p = os.path.join(out, 'lossy.webp'); quad().save(p, quality=90); et(p, *FULL, '-Orientation#=6')
p = os.path.join(out, 'lossless.webp'); quad().save(p, lossless=True); et(p, '-DateTimeOriginal=2022:01:02 03:04:05', '-Orientation#=1')
# GIF
quad().convert('P').save(os.path.join(out, 'plain.gif'))
# HEIC（0.11.1 起 Windows 上用 WIC 真解）。photo_irot.heic（打包里有，这里不生成）是 0.11.1 把这张的 ipco 加 irot（顺时针 90°）手工打的补丁，重新生成打包时要保留
try:
    import pillow_heif
    pillow_heif.register_heif_opener()
    p = os.path.join(out, 'photo.heic')
    exif = Image.Exif()
    exif[0x010F] = 'Apple'; exif[0x0110] = 'iPhone 15 Pro'
    ifd = exif.get_ifd(0x8769)
    ifd[0x9003] = '2024:10:03 14:25:36'; ifd[0x9011] = '-07:00'
    quad().save(p, exif=exif.tobytes(), quality=80)
except ImportError:
    print('没有 pillow-heif，跳过 HEIC')
pack = sys.argv[2] if len(sys.argv) > 2 else None
if pack:
    import base64
    with open(pack, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# 测试照片（tools/claude-kit/gen_photo_fixtures.py 生成，不要手改）。每行：文件名<Tab>base64\n')
        for name in sorted(os.listdir(out)):
            f.write(name + '\t' + base64.b64encode(open(os.path.join(out, name), 'rb').read()).decode('ascii') + '\n')
print('ok')
