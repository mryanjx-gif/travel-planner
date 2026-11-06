// 自动保存的节拍（功能清单 D-07，0.5.0）。照搬 Electron store.js 的 save()：
//   - 改了数据就调用 changed()：马上算"有未保存的改动"（由 DataStore 标记），停手 0.8 秒后才真的保存，
//     连续改几下只保存一次；
//   - changed(Now)：立即保存（Electron 的 opts.now，或者这次改动带了照片）；
//   - 编辑器那类地方可以传自己的停手时间（Electron 约 0.7 秒）。
// 它只负责"什么时候该保存"，发 saveRequested；真正保存、冲突弹框由 AppController 做。
#pragma once

#include <QObject>
#include <QTimer>

namespace tp::data {

class SaveScheduler : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Debounced, Now };
    static constexpr int kDefaultDelayMs = 800;

    explicit SaveScheduler(QObject *parent = nullptr);

    // 有改动。Debounced：重新开始计时（停手 delayMs 后保存）；Now：取消计时，立即发 saveRequested
    void changed(Mode mode = Mode::Debounced, int delayMs = kDefaultDelayMs);
    // 还在等的话立即保存（退出、换文件夹、备份前用）；没在等就什么都不做。返回 true = 发了 saveRequested
    bool flush();
    // 不保存了（重新读取、换了数据文件夹之后）
    void cancel();
    bool pending() const { return m_timer.isActive(); }
    int remainingMs() const { return m_timer.isActive() ? m_timer.remainingTime() : -1; }

signals:
    void saveRequested();

private:
    QTimer m_timer;
};

} // namespace tp::data
