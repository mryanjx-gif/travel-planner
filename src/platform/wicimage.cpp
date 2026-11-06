#include "wicimage.h"

#include <atomic>

#ifdef Q_OS_WIN
#include <QByteArray>
#include <QColorSpace>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace tp::platform::wic {

namespace {
std::atomic_bool g_testOff { false };
}

void setTestOff(bool off) { g_testOff = off; }

#ifdef Q_OS_WIN
namespace {

// 64×48 的测试照片（tests/photos.txt 的 photo.heic，pillow-heif 生成）：探测用，不显示
const char kSample[] = "AAAAHGZ0eXBoZWljAAAAAG1pZjFoZWljbWlhZgAAAb5tZXRhAAAAAAAAACFoZGxyAAAAAAAAAABwaWN0AAAAAAAAAAAAAAAAAAAAADRpbG9jAAAAAERAAAIAAQAAAAAB4gABAAAAAAAAAS4AAgAAAAADEAABAAAAAAAAAIoAAAA4aWluZgAAAAAAAgAAABVpbmZlAgAAAAABAABodmMxAAAAABVpbmZlAgAAAQACAABFeGlmAAAAAA5waXRtAAAAAAABAAAA/WlwcnAAAADdaXBjbwAAAHZodmNDAQNwAAAAkAAAAAAAHvAA/P34+AAADwNgAAEAGEABDAH//wNwAAADAJAAAAMAAAMAHroCQGEAAQAqQgEBA3AAAAMAkAAAAwAAAwAeoCCBBZbq5Ka5uAhoMCAAAAMDIAAAAwAhYgABAAZEAcFzwIkAAAATY29scm5jbHgAAQANAAaAAAAAFGlzcGUAAAAAAAAAQAAAAEAAAAAoY2xhcAAAAEAAAAABAAAAMAAAAAEAAAAAAAAAAv////AAAAACAAAAEHBpeGkAAAAAAwgICAAAABhpcG1hAAAAAAAAAAEAAQWBAgMFhAAAABppcmVmAAAAAAAAAA5jZHNjAAIAAQABAAABwG1kYXQAAAEqKAGvBPjb8uiWt/0Z1///K3+//j23LfcsgvAfjJGl/Dnn/+g7qeEcUvU9JXYZ/6wVWUYMrQbkrzv4riU9xsxVLlKd2ty1qwSRZBfPaacLIwEVoPPTLhSgd+8f2bhMTvdOlvJzSgPJtzI/zk///fev/++Iv61zcDEu/VDUid/43Xn4PRj5TWk/h2lf3Ldhb3b6mYZ/6Es7nrRwhri3J5M2oD8Z+BTLMRRrbaUjoe48ME/YGm3LpBaI1Q8ovmYIyb3pd46OnY/FWLQO57arrovrMCCME6t4hcGPd1m2Mi3Qew0ReJ2huclLWStwu5s0B2yWmkISCBW+gzMjVzO/drASSwqUt/vHXmNVxW3hGdgm0+zZROz2yPkpkkED6JPBm5hkrcgvhYAC93qQpQAAAAZFeGlmAABNTQAqAAAACAADAQ8AAgAAAAYAAAAyARAAAgAAAA4AAAA4h2kABAAAAAEAAABGAAAAAEFwcGxlAGlQaG9uZSAxNSBQcm8AAAKQAwACAAAAFAAAAGSQEQACAAAABwAAAHgAAAAAMjAyNDoxMDowMyAxNDoyNTozNgAtMDc6MDAAAA==";

struct ComScope
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // 主线程已经是 STA 时返回 RPC_E_CHANGED_MODE，照样能用
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

QImage readFrame(IWICImagingFactory *f, IWICBitmapDecoder *dec, int maxSide, HRESULT *err)
{
    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = dec->GetFrame(0, &frame);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || !w || !h) { *err = hr; return {}; }
    ComPtr<IWICBitmapSource> src = frame;
    const UINT longSide = qMax(w, h);
    if (maxSide > 0 && longSide > UINT(maxSide)) {
        const double k = double(maxSide) / longSide;
        w = qMax(1u, UINT(w * k + 0.5));
        h = qMax(1u, UINT(h * k + 0.5));
        ComPtr<IWICBitmapScaler> sc;
        hr = f->CreateBitmapScaler(&sc);
        if (SUCCEEDED(hr)) hr = sc->Initialize(src.Get(), w, h, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) { *err = hr; return {}; }
        src = sc;
    }
    ComPtr<IWICFormatConverter> conv;
    hr = f->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(src.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    QImage img(int(w), int(h), QImage::Format_ARGB32);
    if (SUCCEEDED(hr) && !img.isNull()) hr = conv->CopyPixels(nullptr, UINT(img.bytesPerLine()), UINT(img.sizeInBytes()), img.bits());
    if (FAILED(hr) || img.isNull()) { *err = hr; return {}; }
    // 色彩：iPhone 的照片是 Display P3，带 ICC 才能换回 sRGB（loadOriented 里换）。没有就当 sRGB
    ComPtr<IWICColorContext> ctx;
    UINT n = 0;
    if (SUCCEEDED(f->CreateColorContext(&ctx)) && SUCCEEDED(frame->GetColorContexts(1, ctx.GetAddressOf(), &n)) && n > 0) {
        WICColorContextType type;
        UINT size = 0;
        if (SUCCEEDED(ctx->GetType(&type)) && type == WICColorContextProfile && SUCCEEDED(ctx->GetProfileBytes(0, nullptr, &size)) && size > 0) {
            QByteArray icc(int(size), Qt::Uninitialized);
            if (SUCCEEDED(ctx->GetProfileBytes(size, reinterpret_cast<BYTE *>(icc.data()), &size))) {
                const QColorSpace cs = QColorSpace::fromIccProfile(icc);
                if (cs.isValid()) img.setColorSpace(cs);
            }
        }
    }
    return img;
}

QString hrText(HRESULT hr) { return QStringLiteral("WIC 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')); }

} // namespace

QImage decode(const QString &path, int maxSide, QString *error)
{
    if (g_testOff) { if (error) *error = QStringLiteral("test off"); return {}; }
    ComScope com;
    ComPtr<IWICImagingFactory> f;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    ComPtr<IWICBitmapDecoder> dec;
    if (SUCCEEDED(hr)) hr = f->CreateDecoderFromFilename(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) { if (error) *error = hrText(hr); return {}; }
    const QImage img = readFrame(f.Get(), dec.Get(), maxSide, &hr);
    if (img.isNull() && error) *error = hrText(hr);
    return img;
}

bool heifReady()
{
    if (g_testOff) return false;
    static const bool ready = [] {
        ComScope com;
        const QByteArray bytes = QByteArray::fromBase64(kSample);
        ComPtr<IWICImagingFactory> f;
        ComPtr<IWICStream> st;
        ComPtr<IWICBitmapDecoder> dec;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        if (SUCCEEDED(hr)) hr = f->CreateStream(&st);
        if (SUCCEEDED(hr)) hr = st->InitializeFromMemory(reinterpret_cast<WICInProcPointer>(const_cast<char *>(bytes.constData())), DWORD(bytes.size()));
        if (SUCCEEDED(hr)) hr = f->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec);
        const bool ok = SUCCEEDED(hr) && !readFrame(f.Get(), dec.Get(), 0, &hr).isNull();
        qInfo("HEIC：%s（%s）", ok ? "这台电脑能解" : "这台电脑解不了", qUtf8Printable(hrText(hr)));
        return ok;
    }();
    return ready;
}
#else
QImage decode(const QString &, int, QString *error) { if (error) *error = QStringLiteral("no WIC"); return {}; }
bool heifReady() { return false; }
#endif

} // namespace tp::platform::wic
