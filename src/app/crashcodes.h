// 崩溃处理用的异常码判断（0.5.2）。不依赖 Qt 和 Windows 头文件，单元测试在 Linux 上也能跑。
// TpApplication::notify 外面那层 SEH（src/app/tpapplication.cpp）用它决定"这个异常要不要当崩溃处理"：
//   - C++ 异常（0xE06D7363）不接：留给里层的 catch（出错小框 TP-M）；
//   - 信息类 / 成功类（最高两位 00、01），例如线程命名 0x406D1388、OutputDebugString 0x40010006 / 0x4001000A：不接；
//   - 错误类（最高两位 11，0xC…… 和我们自己的 0xE0545xxx）：接；
//   - 警告类（10）里只接会让程序停下的三种：对齐错误 0x80000002、断点 0x80000003、单步 0x80000004
//     （调试器挂着时外层根本不接，见 CrashHandler::sehFilter）。
#pragma once

#include <cstdint>

namespace tp::crashcodes {

constexpr std::uint32_t kCppException = 0xE06D7363u;   // MSVC 的 throw
constexpr std::uint32_t kSoftBase = 0xE0545000u;       // 我们自己抛的（terminate、纯虚函数、abort、qFatal 等）

constexpr bool isFatal(std::uint32_t code)
{
    if (code == kCppException) return false;
    switch (code >> 30) {
    case 3u: return true;                                                   // 错误
    case 2u: return code == 0x80000002u || code == 0x80000003u || code == 0x80000004u;   // 警告里的三种
    default: return false;                                                  // 成功 / 信息
    }
}

} // namespace tp::crashcodes
