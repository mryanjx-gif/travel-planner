#include "crashhandler.h"

#include <QtGlobal>
#include <cstdlib>

#ifndef Q_OS_WIN

namespace tp::platform::CrashHandler {
void install(const QString &, const QString &, const QString &) {}
void updateSystem(const QString &) {}
void onFatal(const QString &) {}
long sehFilter(void *) { return 0; }
QString checkFilter() { return {}; }
QString status() { return QStringLiteral("只有 Windows 版有崩溃处理"); }
void crashForTest() { std::abort(); }
void crashOnBackgroundThreadForTest() { std::abort(); }
} // namespace tp::platform::CrashHandler

#else

#include "crashcodes.h"
#include "errhash.h"

#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstring>
#include <exception>
#include <stdlib.h>

// 崩溃那一刻不能分配内存、不能用 Qt：下面全部用固定大小的缓冲区和 Win32 API。
namespace tp::platform::CrashHandler {
namespace {

using MiniDumpWriteDumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                            PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// 自定义异常码（'TP' 开头）：绕过过滤器的几种崩溃先抛它，再统一处理
constexpr DWORD kSoftBase = tp::crashcodes::kSoftBase;
constexpr DWORD kTerminate = kSoftBase + 1;
constexpr DWORD kPureCall = kSoftBase + 2;
constexpr DWORD kInvalidParam = kSoftBase + 3;
constexpr DWORD kAbort = kSoftBase + 4;
constexpr DWORD kFatal = kSoftBase + 5;
constexpr int kFrames = 16;

wchar_t g_logDir[1024];
char g_version[64];
char g_system[512];
char g_fatalMsg[512];
MiniDumpWriteDumpFn g_dump = nullptr;
HANDLE g_evStart = nullptr;
HANDLE g_evDone = nullptr;
HANDLE g_thread = nullptr;
DWORD g_writerTid = 0;
DWORD g_mainTid = 0;
volatile LONG g_entered = 0;
EXCEPTION_POINTERS *g_ep = nullptr;
DWORD g_crashTid = 0;

// ---------- 小工具：固定缓冲区里拼字符串 ----------
struct Buf {
    char *p;
    size_t cap;
    size_t n = 0;
    Buf(char *b, size_t c) : p(b), cap(c) { p[0] = 0; }
    Buf &s(const char *t) { while (*t && n + 1 < cap) p[n++] = *t++; p[n] = 0; return *this; }
    Buf &hex(unsigned long long v, int minDigits = 1)
    {
        char t[20];
        int k = 0;
        do { const int d = int(v & 15); t[k++] = char(d < 10 ? '0' + d : 'A' + d - 10); v >>= 4; } while (v && k < 20);
        while (k < minDigits && k < 20) t[k++] = '0';
        while (k) { if (n + 1 < cap) p[n++] = t[--k]; else --k; }
        p[n] = 0;
        return *this;
    }
    Buf &dec(unsigned long long v, int minDigits = 1)
    {
        char t[24];
        int k = 0;
        do { t[k++] = char('0' + v % 10); v /= 10; } while (v && k < 24);
        while (k < minDigits && k < 24) t[k++] = '0';
        while (k) { if (n + 1 < cap) p[n++] = t[--k]; else --k; }
        p[n] = 0;
        return *this;
    }
    Buf &w(const wchar_t *t)   // UTF-16 → UTF-8
    {
        if (n + 1 >= cap) return *this;
        const int m = WideCharToMultiByte(CP_UTF8, 0, t, -1, p + n, int(cap - n), nullptr, nullptr);
        if (m > 0) n += size_t(m - 1);
        p[n] = 0;
        return *this;
    }
};

void copyUtf8(char *dst, size_t cap, const QString &s)
{
    const QByteArray b = s.toUtf8();
    const size_t n = qMin(size_t(b.size()), cap - 1);
    std::memcpy(dst, b.constData(), n);
    dst[n] = 0;
}

const wchar_t *baseName(const wchar_t *path)
{
    const wchar_t *b = path;
    for (const wchar_t *q = path; *q; ++q)
        if (*q == L'\\' || *q == L'/') b = q + 1;
    return b;
}

// 地址 → 模块名（只留文件名）和偏移
bool moduleOf(DWORD64 addr, wchar_t *name, size_t cap, DWORD64 *offset)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &m) || !m) {
        name[0] = 0;
        *offset = addr;
        return false;
    }
    wchar_t full[1024];
    const DWORD n = GetModuleFileNameW(m, full, 1024);
    if (n == 0 || n >= 1024) { name[0] = 0; *offset = addr; return false; }
    const wchar_t *b = baseName(full);
    size_t i = 0;
    for (; b[i] && i + 1 < cap; ++i) name[i] = b[i];
    name[i] = 0;
    *offset = addr - reinterpret_cast<DWORD64>(m);
    return true;
}

bool isSystemModule(const wchar_t *name)
{
    static const wchar_t *const sys[] = { L"ntdll.dll", L"kernelbase.dll", L"kernel32.dll", L"ucrtbase.dll",
                                          L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll", nullptr };
    for (int i = 0; sys[i]; ++i)
        if (lstrcmpiW(name, sys[i]) == 0) return true;
    return false;
}

const char *exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "访问冲突 access violation";
    case EXCEPTION_STACK_OVERFLOW: return "栈溢出 stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "除以零 integer divide by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "非法指令 illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR: return "读文件页失败 in-page error";
    case EXCEPTION_BREAKPOINT: return "断点 breakpoint";
    case 0xE06D7363: return "没接住的 C++ 异常 unhandled C++ exception";
    case kTerminate: return "std::terminate";
    case kPureCall: return "调用了纯虚函数 pure virtual call";
    case kInvalidParam: return "运行库参数错误 invalid CRT parameter";
    case kAbort: return "abort";
    case kFatal: return "Qt 致命错误 qFatal";
    default: return "";
    }
}

// x64：从崩溃时的上下文往回展开最多 kFrames 层（不需要符号文件）
int unwind(const CONTEXT *start, DWORD64 *pcs)
{
    CONTEXT ctx = *start;
    int n = 0;
    while (n < kFrames && ctx.Rip) {
        pcs[n++] = ctx.Rip;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fe) {   // 叶函数：返回地址就在栈顶
            if (!ctx.Rsp) break;
            ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
            ctx.Rsp += 8;
        } else {
            PVOID handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &handlerData, &establisher, nullptr);
        }
    }
    return n;
}

int safeUnwind(const CONTEXT *start, DWORD64 *pcs)
{
#ifdef _MSC_VER
    __try { return unwind(start, pcs); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
#else
    return unwind(start, pcs);
#endif
}

void appendFile(const wchar_t *path, const char *data, size_t n, bool truncate)
{
    HANDLE h = CreateFileW(path, truncate ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           truncate ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, data, DWORD(n), &w, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
}

void joinPath(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name)
{
    size_t i = 0;
    for (const wchar_t *q = dir; *q && i + 2 < cap; ++q) out[i++] = *q;
    if (i && out[i - 1] != L'\\') out[i++] = L'\\';
    for (const wchar_t *q = name; *q && i + 1 < cap; ++q) out[i++] = *q;
    out[i] = 0;
}

// ---------- 真正写报告（在写报告线程里运行；没有线程时在崩溃线程里直接运行） ----------
void writeAll()
{
    const EXCEPTION_RECORD *er = g_ep ? g_ep->ExceptionRecord : nullptr;
    const DWORD code = er ? er->ExceptionCode : kFatal;
    const bool soft = code >= kSoftBase && code <= kSoftBase + 0xFF;

    // 堆栈（模块 + 偏移）
    DWORD64 pcs[kFrames] = {};
    int nf = 0;
    if (g_ep && g_ep->ContextRecord) nf = safeUnwind(g_ep->ContextRecord, pcs);
    if (nf == 0 && er) { pcs[0] = reinterpret_cast<DWORD64>(er->ExceptionAddress); nf = 1; }

    // 错误码用的位置：第一个不是系统 / 运行库的帧；自己抛的那几种先跳过本程序里的两层（抛异常的函数和处理函数）
    wchar_t culprit[260] = L"?";
    DWORD64 culpritOff = 0;
    int skipOwn = soft ? 2 : 0;
    for (int i = 0; i < nf; ++i) {
        wchar_t name[260];
        DWORD64 off = 0;
        if (!moduleOf(pcs[i], name, 260, &off)) continue;
        if (isSystemModule(name)) continue;
        if (skipOwn > 0) { --skipOwn; continue; }
        lstrcpynW(culprit, name, 260);
        culpritOff = off;
        break;
    }
    // 哈希的输入："异常码|模块+0x偏移"（UTF-16）
    wchar_t key[400];
    {
        char tmp[400];
        Buf k(tmp, sizeof tmp);
        k.hex(code, 8).s("|").w(culprit).s("+0x").hex(culpritOff);
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, key, 400);
    }
    char h4[5];
    tp::errhash::hash4(reinterpret_cast<const char16_t *>(key), size_t(lstrlenW(key)), h4);
    char errCode[32];
    Buf(errCode, sizeof errCode).s("TP-CRASH-").s(h4);

    // 时间、转储文件名
    SYSTEMTIME t;
    GetLocalTime(&t);
    char dumpName8[64];
    Buf(dumpName8, sizeof dumpName8).s("crash-").dec(t.wYear, 4).dec(t.wMonth, 2).dec(t.wDay, 2).s("-")
        .dec(t.wHour, 2).dec(t.wMinute, 2).dec(t.wSecond, 2).s(".dmp");
    wchar_t dumpName[64];
    MultiByteToWideChar(CP_UTF8, 0, dumpName8, -1, dumpName, 64);
    wchar_t dumpPath[1200];
    joinPath(dumpPath, 1200, g_logDir, dumpName);

    bool dumped = false;
    if (g_dump) {
        HANDLE f = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei {};
            mei.ThreadId = g_crashTid;
            mei.ExceptionPointers = g_ep;
            mei.ClientPointers = FALSE;
            dumped = g_dump(GetCurrentProcess(), GetCurrentProcessId(), f,
                            MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo), g_ep ? &mei : nullptr, nullptr, nullptr) != FALSE;
            CloseHandle(f);
            if (!dumped) DeleteFileW(dumpPath);
        }
    }

    // 报告（和 Electron errReport 一样的几行 + 崩溃位置、堆栈）
    static char report[8192];
    Buf r(report, sizeof report);
    r.s("旅行计划 错误报告 / Travel Planner error report\n");
    r.s("错误码 Code: ").s(errCode).s("\n");
    r.s("意外退出 Crash: 0x").hex(code, 8);
    if (*exceptionName(code)) r.s(" ").s(exceptionName(code));
    r.s("\n");
    if (code == kFatal && g_fatalMsg[0]) r.s("消息 Message: ").s(g_fatalMsg).s("\n");
    if (code == EXCEPTION_ACCESS_VIOLATION && er && er->NumberParameters >= 2)
        r.s("访问 Access: ").s(er->ExceptionInformation[0] == 0 ? "读 read" : er->ExceptionInformation[0] == 1 ? "写 write" : "执行 execute")
            .s(" 0x").hex(er->ExceptionInformation[1]).s("\n");
    r.s("位置 At: ").w(culprit).s("+0x").hex(culpritOff).s("\n");
    r.s("线程 Thread: ").s(g_crashTid == g_mainTid ? "主线程 main" : "其他线程 other").s(" (").dec(g_crashTid).s(")\n");
    r.s("版本 Version: v").s(g_version).s("\n");
    r.s("系统 System: ").s(g_system).s("\n");
    r.s("时间 Time: ").dec(t.wYear).s("/").dec(t.wMonth).s("/").dec(t.wDay).s(" ").dec(t.wHour, 2).s(":").dec(t.wMinute, 2).s(":").dec(t.wSecond, 2).s("\n");
    r.s("转储 Dump: ").s(dumped ? dumpName8 : "（没写成 not written）").s("\n");
    r.s("堆栈 Stack:\n");
    for (int i = 0; i < nf; ++i) {
        wchar_t name[260];
        DWORD64 off = 0;
        r.s("  ");
        if (moduleOf(pcs[i], name, 260, &off)) r.w(name).s("+0x").hex(off);
        else r.s("0x").hex(pcs[i]);
        r.s("\n");
    }

    // errors.log：===== 时间(UTC) 错误码
    SYSTEMTIME u;
    GetSystemTime(&u);
    static char block[9000];
    Buf b(block, sizeof block);
    b.s("===== ").dec(u.wYear, 4).s("-").dec(u.wMonth, 2).s("-").dec(u.wDay, 2).s("T").dec(u.wHour, 2).s(":")
        .dec(u.wMinute, 2).s(":").dec(u.wSecond, 2).s(".").dec(u.wMilliseconds, 3).s("Z ").s(errCode).s("\n").s(report).s("\n");
    wchar_t path[1200];
    joinPath(path, 1200, g_logDir, L"errors.log");
    appendFile(path, block, b.n, false);

    // 上次崩溃的记号（src/app/crashinfo.cpp 读）
    static char marker[9000];
    Buf m(marker, sizeof marker);
    m.s("code: ").s(errCode).s("\n");
    if (dumped) m.s("dump: ").s(dumpName8).s("\n");
    m.s("\n").s(report);
    joinPath(path, 1200, g_logDir, L"last-crash.txt");
    appendFile(path, marker, m.n, true);
}

DWORD WINAPI writerThread(LPVOID)
{
    WaitForSingleObject(g_evStart, INFINITE);
    writeAll();
    SetEvent(g_evDone);
    return 0;
}

LONG WINAPI filter(EXCEPTION_POINTERS *ep)
{
    // 只处理第一次（写报告时又崩了，直接结束）
    if (InterlockedExchange(&g_entered, 1) != 0) {
        TerminateProcess(GetCurrentProcess(), 0xDEAD);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    g_ep = ep;
    g_crashTid = GetCurrentThreadId();
    if (g_thread && g_crashTid != g_writerTid) {
        SetEvent(g_evStart);
        WaitForSingleObject(g_evDone, 20000);
    } else {
        writeAll();
    }
    TerminateProcess(GetCurrentProcess(), ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0xDEAD);
    return EXCEPTION_EXECUTE_HANDLER;
}

DWORD WINAPI crashThread(LPVOID)
{
    volatile int *volatile p = nullptr;
    *p = 43;
    return 0;
}

__declspec(noinline) void raiseSoft(DWORD code)
{
    RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), code);   // 不会走到这里
}

__declspec(noinline) void onTerminate() { raiseSoft(kTerminate); }
__declspec(noinline) void onPureCall() { raiseSoft(kPureCall); }
__declspec(noinline) void onInvalidParam(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t) { raiseSoft(kInvalidParam); }
__declspec(noinline) void onAbort(int) { raiseSoft(kAbort); }

} // namespace

void install(const QString &logDir, const QString &version, const QString &system)
{
    const QString dir = QString(logDir).replace(QLatin1Char('/'), QLatin1Char('\\'));
    const qsizetype n = qMin<qsizetype>(dir.size(), 1023);
    std::memcpy(g_logDir, dir.utf16(), size_t(n) * sizeof(wchar_t));
    g_logDir[n] = 0;
    copyUtf8(g_version, sizeof g_version, version);
    copyUtf8(g_system, sizeof g_system, system);
    g_mainTid = GetCurrentThreadId();

    // dbghelp.dll 是 Windows 自带的（system32），不打包
    wchar_t sysDir[MAX_PATH];
    const UINT sn = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (sn > 0 && sn < MAX_PATH - 20) {
        wchar_t p[MAX_PATH + 20];
        joinPath(p, MAX_PATH + 20, sysDir, L"dbghelp.dll");
        if (HMODULE m = LoadLibraryW(p))
            g_dump = reinterpret_cast<MiniDumpWriteDumpFn>(reinterpret_cast<void *>(GetProcAddress(m, "MiniDumpWriteDump")));
    }

    g_evStart = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_evDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_evStart && g_evDone)
        g_thread = CreateThread(nullptr, 256 * 1024, writerThread, nullptr, 0, &g_writerTid);

    // 栈溢出时过滤器自己也要一点栈
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);

    SetUnhandledExceptionFilter(filter);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParam);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, onAbort);
}

long sehFilter(void *exceptionPointers)
{
    auto *ep = static_cast<EXCEPTION_POINTERS *>(exceptionPointers);
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (IsDebuggerPresent()) return EXCEPTION_CONTINUE_SEARCH;
    if (!tp::crashcodes::isFatal(ep->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
    return filter(ep);   // 写报告、结束进程
}

QString checkFilter()
{
    // 没有"只读"的 API：先换成我们的，看换下来的是谁
    const LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(filter);
    if (prev == filter) return {};
    if (!prev) return QStringLiteral("(空)");
    wchar_t name[260];
    DWORD64 off = 0;
    if (!moduleOf(reinterpret_cast<DWORD64>(prev), name, 260, &off))
        return QStringLiteral("0x%1").arg(quint64(reinterpret_cast<quintptr>(prev)), 0, 16);
    return QString::fromWCharArray(name) + QStringLiteral("+0x") + QString::number(quint64(off), 16);
}

QString status()
{
    const QString replaced = checkFilter();
    return QStringLiteral("dbghelp %1，写报告的线程 %2，过滤器 %3")
        .arg(g_dump ? QStringLiteral("已加载") : QStringLiteral("没加载（不会写 .dmp）"),
             g_thread ? QStringLiteral("已建好") : QStringLiteral("没建成（改在崩溃的线程里写）"),
             replaced.isEmpty() ? QStringLiteral("是我们的") : QStringLiteral("被换成了 %1（已换回）").arg(replaced));
}

void crashOnBackgroundThreadForTest()
{
    if (HANDLE h = CreateThread(nullptr, 0, crashThread, nullptr, 0, nullptr)) CloseHandle(h);
}

void updateSystem(const QString &system)
{
    copyUtf8(g_system, sizeof g_system, system);
}

void onFatal(const QString &message)
{
    copyUtf8(g_fatalMsg, sizeof g_fatalMsg, message.left(400));
    raiseSoft(kFatal);
}

void crashForTest()
{
    volatile int *volatile p = nullptr;
    *p = 42;
    std::abort();
}

} // namespace tp::platform::CrashHandler

#endif
