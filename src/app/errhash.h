// 错误码的哈希（0.5.1）：和 Electron 版 errors.js / main.js 的 hash() 完全一样——
//   FNV-1a 32 位，逐个 UTF-16 码元（JS 的 charCodeAt）→ 无符号转 36 进制 → 大写 → 取最后 4 位，不足 4 位前面补 0。
// 不依赖 Qt，崩溃处理（src/platform/crashhandler.cpp，在崩溃那一刻不能用 Qt、不能分配内存）也用它。
#pragma once

#include <cstddef>
#include <cstdint>

namespace tp::errhash {

// 把 s[0..n) 算成 4 个字符写进 out（out 至少 5 个 char，结尾补 0）
inline void hash4(const char16_t *s, std::size_t n, char out[5])
{
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= std::uint32_t(s[i]);
        h *= 16777619u;   // 和 Math.imul 一样取低 32 位
    }
    char buf[16];
    int len = 0;
    do {
        const unsigned d = h % 36u;
        buf[len++] = char(d < 10 ? '0' + d : 'A' + (d - 10));
        h /= 36u;
    } while (h != 0);
    // buf 里是倒序的 36 进制；取最低的 4 位，不足补 0
    for (int i = 0; i < 4; ++i) out[3 - i] = i < len ? buf[i] : '0';
    out[4] = 0;
}

} // namespace tp::errhash
