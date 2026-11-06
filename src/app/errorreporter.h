// 出错提示（0.5.1，D-23）：没接住的错误 → 右下角小框（方案 B，QML 那边是 qml/components/ErrorCard.qml）。
// 规则照 Electron 版 app/js/errors.js：
//   - 错误码 TP-<种类>-xxxx：同一种错误（同样的错误第一行 + 出错的文件和行）每次得到同一个码；
//     种类 Q = QML 里的 JS 错误，M = C++ 异常（Electron 是 R / P = 页面脚本，M = 主进程）。
//   - 同一个错误码一次打开只弹一次，一次打开最多弹 3 个；断网、连不上服务器这类不弹。
//   - 每个错误都写进 errors.log（logs 文件夹，超过 256 KB 留后一半）。
//   - 报告：版本、系统、时间、页面、错误、堆栈（完整路径只留文件名，不带出用户名和文件夹）。
// 只依赖 Qt Core（单元测试直接用）。QML 警告、C++ 异常怎么接进来见 main.cpp / tpapplication.cpp。
#pragma once

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

namespace tp::app {

class ErrorReporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)

public:
    static constexpr int kMaxPopups = 3;

    explicit ErrorReporter(QObject *parent = nullptr);
    ~ErrorReporter() override;
    static ErrorReporter *instance();          // 最近创建的那个（C++ 异常处理里用）

    void setVersion(const QString &v) { m_version = v; }
    void setSystem(const QString &s);          // 系统、Qt 版本、图形后端（显卡名在窗口初始化后补上）
    QString system() const;
    void setLogFile(const QString &path) { m_logFile = path; }   // 空 = Logger 的 errors.log
    QString page() const { return m_page; }
    void setPage(const QString &p) { if (p != m_page) { m_page = p; emit pageChanged(); } }

    // 报告一个错误；返回错误码（不算错误、被忽略时返回空）。stack 里每行一个位置，形如 "at foo (Main.qml:12:5)"；
    // at 非空时直接用它算错误码（没有堆栈的 C++ 异常用）
    QString report(const QString &kind, const QString &message, const QString &stack = QString(),
                   const QString &at = QString());
    // QML 引擎的警告：真正的 JS 异常（TypeError、ReferenceError…）走 report("Q")，其余只写日志。返回错误码或空
    QString reportQml(const QString &description, const QString &url, int line, int column);

    int popupCount() const { return m_popups; }
    // 只写 errors.log、不弹小框（显卡出错时窗口画不出来，由系统对话框显示）；返回报告全文
    QString logOnly(const QString &code, const QString &message);

    // ---- 算法（照 errors.js，公开给测试） ----
    static QString hash(const QString &s);                        // FNV-1a → 4 位
    static bool ignored(const QString &message);                  // 断网类、ResizeObserver 等不算
    static QString cleanStack(const QString &stack);              // 路径只留文件名，最多 10 行
    static QString codeOf(const QString &kind, const QString &message, const QString &stack);
    static bool isJsException(const QString &description);        // QML 警告里哪些算"没接住的 JS 错误"
    QString buildReport(const QString &code, const QString &message, const QString &stack) const;

signals:
    void popup(const QString &code, const QString &message, const QString &report);
    void pageChanged();
    void systemChanged();

private:
    void writeLog(const QString &code, const QString &report);

    mutable QMutex m_mutex;
    QSet<QString> m_shown;
    int m_popups = 0;
    QString m_version;
    QString m_system;
    QString m_page = QStringLiteral("map");
    QString m_logFile;
};

} // namespace tp::app
