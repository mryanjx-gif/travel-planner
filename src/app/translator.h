// 界面翻译（0.4.0，PO 2026-11-02：不用 lupdate / lrelease，不生成 .ts / .qm）。
// 界面文字用 qsTr("中文")（QML）或 tp::tr("中文")（C++）写，键就是中文原文；英文来自 i18n/tr/*.txt：
//   每行 `中文<Tab>英文`；先按文件名字典序读 Electron 词典（electron-v6.3.0/b*.txt，和 Electron build_dict.py
//   一样后面的覆盖前面的），最后读 native.txt（原生版新加的，同一句以它为准）。
//   native.txt 里 `\n` 表示换行、`\t` 表示 Tab；英文为空的行忽略。
// 这些文件打包进程序资源（:/i18n/tr/…），运行时直接读。
#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

namespace tp {

// C++ 里的界面文字：tp::tr("中文")。检查脚本（tests/tst_app.cpp allStringsTranslated）扫描 qsTr( 和 tp::tr(
QString tr(const char *zh);

namespace app {

class DictTranslator : public QTranslator
{
public:
    // dir 里找 *.txt（含一层子文件夹）；返回读到的条目数
    int loadDir(const QString &dir);
    void insert(const QString &zh, const QString &en) { m_map.insert(zh, en); }
    bool contains(const QString &zh) const { return m_map.contains(zh); }
    QString lookup(const QString &zh) const { return m_map.value(zh); }
    int size() const { return int(m_map.size()); }

    bool isEmpty() const override { return m_map.isEmpty(); }
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;

    // 解析一个词典文件的内容（测试用）；native = true 时处理 \n、\t
    static QHash<QString, QString> parse(const QByteArray &bytes, bool native);

private:
    QHash<QString, QString> m_map;
};

} // namespace app
} // namespace tp
