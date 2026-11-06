#include "procmem.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <vector>

namespace {
// 显存读数（0.8.6）：IDXGIAdapter3::QueryVideoMemoryInfo 给的是"本进程"在这块显卡上的用量，和用哪种图形接口无关
// （只查询，不建设备、不画东西）。dxgi.dll 运行时再加载，不改链接设置；拿不到就返回 -1，内存测试照常跑。
struct GpuQuery {
    std::vector<IDXGIAdapter3 *> adapters;
    bool tried = false;
    void init()
    {
        tried = true;
        HMODULE dll = LoadLibraryW(L"dxgi.dll");
        if (!dll) return;
        using Create = HRESULT(WINAPI *)(REFIID, void **);
        const auto create = reinterpret_cast<Create>(reinterpret_cast<void *>(GetProcAddress(dll, "CreateDXGIFactory1")));
        if (!create) return;
        IDXGIFactory1 *f = nullptr;
        if (FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&f))) || !f) return;
        IDXGIAdapter1 *a = nullptr;
        for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            IDXGIAdapter3 *a3 = nullptr;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                && SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&a3))) && a3)
                adapters.push_back(a3);
            a->Release();
        }
        f->Release();
    }
};
GpuQuery g_gpu;
} // namespace

ProcMem readProcMem()
{
    ProcMem m;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
        m.workingSetMB = pmc.WorkingSetSize / 1048576.0;
        m.privateMB = pmc.PrivateUsage / 1048576.0;
    }
    if (!g_gpu.tried) g_gpu.init();
    if (!g_gpu.adapters.empty()) {
        double loc = 0, non = 0;
        bool ok = false;
        for (IDXGIAdapter3 *a : g_gpu.adapters) {
            DXGI_QUERY_VIDEO_MEMORY_INFO i{};
            if (SUCCEEDED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &i))) { loc += i.CurrentUsage; ok = true; }
            if (SUCCEEDED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &i))) { non += i.CurrentUsage; ok = true; }
        }
        if (ok) { m.gpuLocalMB = loc / 1048576.0; m.gpuNonLocalMB = non / 1048576.0; }
    }
    return m;
}
#else
#include <QFile>
#include <QByteArray>

ProcMem readProcMem()
{
    ProcMem m;
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return m;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const auto kb = [&line] { return line.simplified().split(' ').value(1).toDouble() / 1024.0; };
        if (line.startsWith("VmRSS:")) m.workingSetMB = kb();
        else if (line.startsWith("RssAnon:")) m.privateMB = kb();
    }
    return m;
}
#endif
