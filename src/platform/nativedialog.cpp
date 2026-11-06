#include "nativedialog.h"

#include <QtGlobal>

#ifndef Q_OS_WIN

#include <QDebug>

namespace tp::platform::NativeDialog {
void prepare(const QString &) {}
int show(const Spec &spec)
{
    qWarning("系统对话框（这个系统上不显示）：%s / %s → 按 %d", qUtf8Printable(spec.heading),
             qUtf8Printable(spec.content.left(200)), spec.cancelIndex);
    return spec.cancelIndex >= 0 ? spec.cancelIndex : spec.defaultIndex;
}
bool copyText(const QString &) { return false; }
} // namespace tp::platform::NativeDialog

#else

#include <QDir>
#include <QFile>
#include <windows.h>
#include <commctrl.h>
#include <cstring>
#include <string>
#include <vector>

namespace tp::platform::NativeDialog {
namespace {

using TaskDialogIndirectFn = HRESULT(WINAPI *)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
HANDLE g_actCtx = INVALID_HANDLE_VALUE;
TaskDialogIndirectFn g_taskDialog = nullptr;
constexpr int kIdBase = 100;

std::wstring ws(const QString &s) { return s.toStdWString(); }

struct CallbackData {
    const Spec *spec;
};

HRESULT CALLBACK callback(HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR ref)
{
    auto *d = reinterpret_cast<CallbackData *>(ref);
    if (msg == TDN_CREATED) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(hwnd);
    } else if (msg == TDN_TIMER && d && d->spec->autoClose && d->spec->autoIndex >= 0) {
        if (d->spec->autoClose())
            PostMessageW(hwnd, TDM_CLICK_BUTTON, WPARAM(kIdBase + d->spec->autoIndex), 0);
    }
    return S_OK;
}

int showTaskDialog(const Spec &spec)
{
    const std::wstring title = ws(spec.title), heading = ws(spec.heading), content = ws(spec.content);
    std::vector<std::wstring> texts;
    for (const QString &b : spec.buttons) texts.push_back(ws(b));
    std::vector<TASKDIALOG_BUTTON> btns;
    for (size_t i = 0; i < texts.size(); ++i) btns.push_back({ int(kIdBase + i), texts[i].c_str() });

    CallbackData data { &spec };
    TASKDIALOGCONFIG cfg {};
    cfg.cbSize = sizeof cfg;
    cfg.hwndParent = nullptr;   // 主线程可能卡住：不挂在主窗口上
    cfg.dwFlags = TDF_CALLBACK_TIMER | TDF_SIZE_TO_CONTENT | (spec.cancelIndex >= 0 ? TDF_ALLOW_DIALOG_CANCELLATION : 0);
    cfg.pszWindowTitle = title.c_str();
    cfg.pszMainIcon = spec.warning ? TD_WARNING_ICON : TD_ERROR_ICON;
    cfg.pszMainInstruction = heading.c_str();
    cfg.pszContent = content.c_str();
    cfg.cButtons = UINT(btns.size());
    cfg.pButtons = btns.data();
    cfg.nDefaultButton = kIdBase + spec.defaultIndex;
    cfg.pfCallback = callback;
    cfg.lpCallbackData = reinterpret_cast<LONG_PTR>(&data);

    ULONG_PTR cookie = 0;
    const bool act = g_actCtx != INVALID_HANDLE_VALUE && ActivateActCtx(g_actCtx, &cookie);
    int pressed = 0;
    const HRESULT hr = g_taskDialog(&cfg, &pressed, nullptr, nullptr);
    if (act) DeactivateActCtx(0, cookie);
    if (FAILED(hr)) return -2;
    if (pressed >= kIdBase && pressed < kIdBase + int(btns.size())) return pressed - kIdBase;
    return spec.cancelIndex >= 0 ? spec.cancelIndex : spec.defaultIndex;
}

// 退路：MessageBox 只有固定按钮，正文末尾写明对应关系
int showMessageBox(const Spec &spec)
{
    QString text = spec.heading + QStringLiteral("\n\n") + spec.content;
    UINT type = MB_TOPMOST | MB_SETFOREGROUND | (spec.warning ? MB_ICONWARNING : MB_ICONERROR);
    const int n = int(spec.buttons.size());
    if (n >= 3) {
        type |= MB_YESNOCANCEL;
        text += QStringLiteral("\n\n[是 Yes] %1\n[否 No] %2\n[取消 Cancel] %3").arg(spec.buttons[0], spec.buttons[1], spec.buttons[2]);
    } else if (n == 2) {
        type |= MB_YESNO;
        text += QStringLiteral("\n\n[是 Yes] %1\n[否 No] %2").arg(spec.buttons[0], spec.buttons[1]);
    } else {
        type |= MB_OK;
    }
    const int r = MessageBoxW(nullptr, ws(text).c_str(), ws(spec.title).c_str(), type);
    if (n >= 3) return r == IDYES ? 0 : r == IDNO ? 1 : 2;
    if (n == 2) return r == IDYES ? 0 : 1;
    return 0;
}

} // namespace

void prepare(const QString &cacheDir)
{
    const QString path = cacheDir + QStringLiteral("/comctl6.manifest");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
                "  <dependency><dependentAssembly>\n"
                "    <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\""
                " processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\n"
                "  </dependentAssembly></dependency>\n"
                "</assembly>\n");
        f.close();
    }
    const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    ACTCTXW ctx {};
    ctx.cbSize = sizeof ctx;
    ctx.lpSource = wpath.c_str();
    g_actCtx = CreateActCtxW(&ctx);
    ULONG_PTR cookie = 0;
    const bool act = g_actCtx != INVALID_HANDLE_VALUE && ActivateActCtx(g_actCtx, &cookie);
    if (HMODULE m = LoadLibraryW(L"comctl32.dll"))
        g_taskDialog = reinterpret_cast<TaskDialogIndirectFn>(reinterpret_cast<void *>(GetProcAddress(m, "TaskDialogIndirect")));
    if (act) DeactivateActCtx(0, cookie);
    if (!g_taskDialog) qWarning("TaskDialogIndirect 不可用（通用控件 6.0 没加载上），系统对话框改用 MessageBox");
}

int show(const Spec &spec)
{
    if (g_taskDialog) {
        const int r = showTaskDialog(spec);
        if (r != -2) return r;
    }
    return showMessageBox(spec);
}

bool copyText(const QString &text)
{
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const std::wstring w = ws(text);
    const SIZE_T bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (h) {
        if (void *p = GlobalLock(h)) {
            memcpy(p, w.c_str(), bytes);
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

} // namespace tp::platform::NativeDialog

#endif
