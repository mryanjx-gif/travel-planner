// 进程内存读数（内存测试用）。Windows：工作集 / 私有字节 / 显存（DXGI）；Linux（云端）：VmRSS / RssAnon，没有显存。
#pragma once
#include <QtGlobal>

struct ProcMem {
    double workingSetMB = 0;   // 工作集（任务管理器"内存"列的口径之一）
    double privateMB = 0;      // 私有字节（提交的私有内存，判断泄漏主要看这个）
    // 本进程占的显存（0.8.6）：所有硬件显卡加起来。local = 显卡自己的显存（集成显卡上也算在这里，口径以驱动为准），
    // nonLocal = 借用的系统内存。拿不到时是 -1
    double gpuLocalMB = -1;
    double gpuNonLocalMB = -1;
};

ProcMem readProcMem();
