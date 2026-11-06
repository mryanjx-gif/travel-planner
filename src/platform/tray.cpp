#include "tray.h"

#ifdef Q_OS_WIN
#include <QDir>
#include <QHash>
#include <windows.h>
#include <shellapi.h>

namespace tp::platform {
namespace {
constexpr UINT kCallback = WM_APP + 41;
constexpr UINT kIconId = 1;
constexpr UINT kCmdOpen = 1, kCmdQuit = 2;
QHash<HWND, TrayIcon *> g_owners;
const wchar_t kClass[] = L"TravelPlannerNativeTray";

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (TrayIcon *t = g_owners.value(h)) return LRESULT(t->handleMessage(msg, wp, lp));
    return DefWindowProcW(h, msg, wp, lp);
}
} // namespace

TrayIcon::TrayIcon(QObject *parent) : QObject(parent) {}
TrayIcon::~TrayIcon() { hide(); }

bool TrayIcon::addIcon()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = HWND(m_hwnd);
    nid.uID = kIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kCallback;
    nid.hIcon = HICON(m_icon);
    const std::wstring tip = m_tooltip.left(120).toStdWString();
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

bool TrayIcon::show(const QString &iconPath, const QString &tooltip, const QString &openText, const QString &quitText)
{
    if (m_visible) return true;
    m_tooltip = tooltip; m_openText = openText; m_quitText = quitText;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = inst;
        wc.lpszClassName = kClass;
        registered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!m_hwnd) {
        // 普通的隐藏窗口（不用 HWND_MESSAGE：消息窗口收不到资源管理器重启时广播的 TaskbarCreated）
        m_hwnd = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
        if (!m_hwnd) return false;
        g_owners.insert(HWND(m_hwnd), this);
        m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    }
    if (!m_icon) {
        const int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
        m_icon = LoadImageW(nullptr, reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(iconPath).utf16()), IMAGE_ICON, cx, cy, LR_LOADFROMFILE);
        if (!m_icon) m_icon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
    }
    m_visible = addIcon();
    return m_visible;
}

void TrayIcon::hide()
{
    if (m_visible) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = HWND(m_hwnd);
        nid.uID = kIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_visible = false;
    }
    if (m_hwnd) { g_owners.remove(HWND(m_hwnd)); DestroyWindow(HWND(m_hwnd)); m_hwnd = nullptr; }
    if (m_icon) { DestroyIcon(HICON(m_icon)); m_icon = nullptr; }
}

long long TrayIcon::handleMessage(unsigned msg, unsigned long long wParam, long long lParam)
{
    HWND h = HWND(m_hwnd);
    if (msg == kCallback) {
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            emit openRequested();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kCmdOpen, reinterpret_cast<LPCWSTR>(m_openText.utf16()));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kCmdQuit, reinterpret_cast<LPCWSTR>(m_quitText.utf16()));
            SetMenuDefaultItem(menu, kCmdOpen, FALSE);
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(h);   // 否则点菜单外面菜单不消失（微软文档的已知要求）
            const UINT cmd = UINT(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr));
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd == kCmdOpen) emit openRequested();
            else if (cmd == kCmdQuit) emit quitRequested();
            return 0;
        }
        default:
            return 0;
        }
    }
    if (m_taskbarCreated && msg == m_taskbarCreated && m_visible) {   // 资源管理器重启后重新加图标
        addIcon();
        return 0;
    }
    return DefWindowProcW(h, msg, WPARAM(wParam), LPARAM(lParam));
}

} // namespace tp::platform

#else   // 非 Windows：没有托盘

namespace tp::platform {
TrayIcon::TrayIcon(QObject *parent) : QObject(parent) {}
TrayIcon::~TrayIcon() = default;
bool TrayIcon::show(const QString &, const QString &, const QString &, const QString &) { return false; }
void TrayIcon::hide() {}
} // namespace tp::platform

#endif
