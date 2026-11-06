// 卡住检测的计数逻辑（0.5.1，D-23）：看门狗线程每秒调一次 step()，传入主线程的"心跳"计数。
// 心跳连续 limit 次没变 → Hung（弹"好像卡住了"）；之后心跳一变 → Recovered。
// 恢复后 cooldown 次之内再卡住只记日志、不再弹框（Electron：恢复响应 60 秒后才会再问）。
// suspended = 这一秒不算（调试器挂着、窗口还没显示等），计数清零。
// 用"看门狗自己数了几次"而不是钟表时间：电脑睡眠时两边都停，醒来不会误报。
#pragma once

#include <cstdint>

namespace tp::app {

class HangDetector
{
public:
    enum Result { Ok, Hung, HungQuiet, Recovered };

    explicit HangDetector(int limit = 10, int cooldown = 60) : m_limit(limit), m_cooldown(cooldown) {}

    Result step(std::uint64_t beat, bool suspended = false)
    {
        if (m_cool > 0) --m_cool;
        if (beat != m_last) {
            m_last = beat;
            m_missed = 0;
            if (m_hung) {
                m_hung = false;
                m_cool = m_cooldown;
                return Recovered;
            }
            return Ok;
        }
        if (suspended && !m_hung) { m_missed = 0; return Ok; }
        if (m_hung) return Ok;
        if (++m_missed >= m_limit) {
            m_hung = true;
            return m_cool > 0 ? HungQuiet : Hung;
        }
        return Ok;
    }

    bool hung() const { return m_hung; }
    int missed() const { return m_missed; }

private:
    int m_limit;
    int m_cooldown;
    int m_cool = 0;
    int m_missed = 0;
    bool m_hung = false;
    std::uint64_t m_last = ~std::uint64_t(0);
};

} // namespace tp::app
