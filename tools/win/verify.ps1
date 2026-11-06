# 一键验证（0.7.2；0.7.3 加记号检查；0.8.0 加退出检查）：编译（程序 + 单元测试，同一个编译目录）→ 单元测试 → 自检（在数据副本上自动走一遍流程）→ 截图。
# 由 验证.bat 调用。结果都放在源码目录的 验证结果\（自检报告.txt、*.png）和 验证-结果.txt，Claude 自己读。
# 不碰你的数据：自检和截图都用 D:\TravelPlannerNative-dev\selftest\data（每次从 -DataDir 复制一份新的）。
# PowerShell 5.1 需要本文件带 UTF-8 BOM。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Qt = 'D:\Qt\6.12.0\msvc2022_64',
    [string]$DataDir = 'D:\旅行计划Native数据'
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
$Dev = (Split-Path $Root -Qualifier) + '\TravelPlannerNative-dev'
$App = Join-Path $Dev 'app'
$Exe = Join-Path $App 'TravelPlanner.exe'
$Out = Join-Path $Root '验证结果'
$Copy = Join-Path $Dev 'selftest\data'
$Log = Join-Path $Root '验证-结果.txt'

# 这个版本要截的页面（页面|语言[|hide]）。每个版本按改动更新这里；street 顺便确认真实显卡上地图能画出来。
# |hide：截完先把窗口藏起来再退出（等于从托盘退出；0.8.0 检查退出时地图释放得对不对）
$Pages = @(
    'street|zh',
    'globe|zh', 'morph-half|zh', 'morph-flat|zh', 'morph-ref|zh',
    'filter|zh', 'filter-overlay|zh', 'filter-overlay|en',
    'street|zh|hide'
)
$Logs = Join-Path $env:LOCALAPPDATA 'TravelPlannerNative\logs'
$vStart = Get-Date
# 这次验证期间新出现的崩溃转储（程序启动时只留最近 3 个，所以按时间算，不按个数）
function New-Dumps { if (Test-Path -LiteralPath $Logs) { @(Get-ChildItem -LiteralPath $Logs -Filter 'crash-*.dmp' -File | Where-Object { $_.LastWriteTime -gt $vStart }).Count } else { 0 } }
$appLog = Join-Path $Logs 'app.log'

Set-Content -Path $Log -Value "验证  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Log -Value $m -Encoding UTF8 }
$t0 = Get-Date
function Elapsed { '{0:N0} 秒' -f ((Get-Date) - $t0).TotalSeconds }

# 0.7.1 的遗留：tests\photos\ 里 14 张被加了内容凭证的旧测试照片不再用，移进【删除】（只移动，不删除）
$oldPhotos = Join-Path $Root 'tests\photos'
if (Test-Path -LiteralPath $oldPhotos) {
    $dest = Join-Path $Root ('【删除】\旧文件-' + (Get-Date -Format 'yyyyMMdd-HHmm') + '\tests')
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    try { Move-Item -LiteralPath $oldPhotos -Destination $dest; Say "  [i] 旧的 tests\photos\ 已移进 $dest" } catch { Say "  [!] tests\photos\ 没移动：$($_.Exception.Message)" 'Yellow' }
}

# 0.8.0：原型的地图文件不再用（protomap.*、MapLayer.qml），移进【删除】（只移动，不删除）
$obsolete = @('src\map\protomap.cpp', 'src\map\protomap.h', 'qml\MapLayer.qml') | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) }
if ($obsolete) {
    $dest = Join-Path $Root ('【删除】\旧文件-' + (Get-Date -Format 'yyyyMMdd-HHmm'))
    foreach ($rel in $obsolete) {
        $to = Join-Path $dest (Split-Path $rel -Parent)
        New-Item -ItemType Directory -Force -Path $to | Out-Null
        try { Move-Item -LiteralPath (Join-Path $Root $rel) -Destination $to; Say "  [i] 不再用的 $rel 已移进 $dest" } catch { Say "  [!] $rel 没移动：$($_.Exception.Message)" 'Yellow' }
    }
}

# ---- 1. 编译 ----
Say '[1/4] 编译程序和单元测试（详细输出在 编译-结果.txt）…' 'Cyan'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-app.ps1') -Root $Root -Qt $Qt -WithTests -NoExplorer
$rcBuild = $LASTEXITCODE
if ($rcBuild -ne 0) {
    Say "[×] 编译没成功（退出码 $rcBuild），后面的步骤不做了。告诉 Claude 就行（它会读 编译-结果.txt）。" 'Red'
    exit 1
}
Say "  编译完成（$(Elapsed)）"

# ---- 2. 单元测试 ----
Say '[2/4] 单元测试（详细输出在 单元测试-结果.txt）…' 'Cyan'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run-tests.ps1') -Root $Root -Qt $Qt -Build $App -NoBuild
$rcTests = $LASTEXITCODE
if ($rcTests -eq 0) { Say "  单元测试全部通过（$(Elapsed)）" 'Green' } else { Say "  [!] 单元测试有 $rcTests 项没通过，接着做自检和截图" 'Yellow' }

# ---- 3. 自检 ----
Say '[3/4] 自检（在数据副本上自动导入、旋转、清理……）…' 'Cyan'
if (Test-Path $Out) { Get-ChildItem -LiteralPath $Out -File | Remove-Item -Force }   # 上一次的结果（程序生成的，下面会重新生成）
New-Item -ItemType Directory -Force -Path $Out | Out-Null
function Fresh-Copy {
    if (-not (Test-Path (Join-Path $DataDir 'travel.json'))) { Say "  [×] $DataDir 里没有 travel.json，自检需要一份原生版数据" 'Red'; return $false }
    New-Item -ItemType Directory -Force -Path $Copy | Out-Null
    # /MIR 让副本和原数据一样（副本是我们自己的临时文件夹）；.lock 不复制，免得副本被当成"别处开着"
    $null = & robocopy $DataDir $Copy /MIR /XF .lock /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    return ($LASTEXITCODE -lt 8)
}
# 运行程序并等它退出（超时就结束它）。返回退出码，超时返回 -1
function Run-App([string[]]$argv, [int]$timeoutSec) {
    $p = Start-Process -FilePath $Exe -ArgumentList $argv -PassThru -WorkingDirectory $App
    $null = $p.Handle   # PowerShell 5.1：不先取一次句柄，退出后 ExitCode 可能是空的
    if (-not $p.WaitForExit($timeoutSec * 1000)) { try { $p.Kill() } catch {}; return -1 }
    return $p.ExitCode
}
$report = Join-Path $Out '自检报告.txt'
$rcSelf = -2
if (Fresh-Copy) {
    $rcSelf = Run-App @('--data', "`"$Copy`"", '--selftest', "`"$report`"") 600
    if ($rcSelf -eq 0) { Say "  自检全部通过（$(Elapsed)）" 'Green' }
    elseif ($rcSelf -eq -1) { Say '  [!] 自检 10 分钟还没做完，已经结束它' 'Yellow' }
    elseif ($rcSelf -gt 200 -or $rcSelf -lt -1) { Say ("  [!] 自检程序退出时出错（退出码 0x{0:X8}）" -f $rcSelf) 'Yellow' }
    else { Say "  [!] 自检有 $rcSelf 项没通过（见 验证结果\自检报告.txt）" 'Yellow' }
    if (Test-Path $report) { Get-Content $report -Encoding UTF8 | Where-Object { $_ -match '^(✗|# 结果)' } | ForEach-Object { Say "    $_" } }
}

# ---- 4. 截图 ----
Say "[4/4] 截图（$($Pages.Count) 张，每张约 5–12 秒；期间会有窗口闪出来，不用管）…" 'Cyan'
$shots = 0
if (Fresh-Copy) {
    foreach ($pl in $Pages) {
        $page, $lang, $opt = $pl.Split('|')
        $png = Join-Path $Out ("$page-$lang" + $(if ($opt) { "-$opt" } else { '' }) + '.png')
        $argv = @('--data', "`"$Copy`"", '--lang', $lang, '--screenshot', $page, "`"$png`"")
        if ($opt -eq 'hide') { $argv += '--hide-before-exit' }
        $rc = Run-App $argv 60
        if ((Test-Path $png) -and $rc -eq 0) { $shots++ }
        elseif (Test-Path $png) { Say ("  [!] $page（$lang）截到了，但程序退出时出错（退出码 0x{0:X8}）" -f $rc) 'Yellow' }
        else { Say "  [!] $page（$lang）没截到（退出码 $rc）" 'Yellow' }
    }
}
Say "  截了 $shots / $($Pages.Count) 张（$(Elapsed)）"
# 0.7.3：程序正常退出时应清掉"Vulkan 启动中"记号；还在说明有一次没正常退出（下次启动会退回 OpenGL）
$flag = Join-Path $App 'cache\vulkan-starting.flag'
if (Test-Path -LiteralPath $flag) { Say '  [!] 程序目录 cache\ 里留着 vulkan-starting.flag：有一次运行没正常退出' 'Yellow'; Remove-Item -LiteralPath $flag -Force }
# 0.8.0：退出是否干净（以前跳过析构；现在先在渲染线程上释放地图再正常析构）
$newDumps = New-Dumps
$exitOk = $newDumps -le 0
$released = 0; $forced = 0
if (Test-Path -LiteralPath $appLog) {
    # 0.9.3：按时间取这次验证期间写的行（以前按启动前的行数往后数；app.log 超过上限会被截短，0.9.3 验证时就数成了 0 次）
    $since = $vStart.ToString('yyyy-MM-dd HH:mm:ss')
    $tail = @(Get-Content -LiteralPath $appLog -Encoding UTF8 | Where-Object { $_.Length -ge 19 -and [string]::CompareOrdinal($_.Substring(0, 19), $since) -ge 0 })
    $released = @($tail | Where-Object { $_ -match '地图已释放，开始析构' }).Count
    $forced = @($tail | Where-Object { $_ -match '退出时析构超过' }).Count
}
if ($exitOk -and $forced -eq 0) { Say "  退出检查：$released 次运行都正常退出，没有新的崩溃转储" 'Green' }
else { Say "  [!] 退出检查：新的崩溃转储 $newDumps 个，析构超时 $forced 次（共 $released 次运行走到了释放地图）" 'Yellow' }

Say ''
$ok = ($rcTests -eq 0) -and ($rcSelf -eq 0) -and ($shots -eq $Pages.Count) -and $exitOk -and ($forced -eq 0)
Say ("验证完成，用时 $(Elapsed)：单元测试" + $(if ($rcTests -eq 0) { '通过' } else { "$rcTests 项没过" }) +
     '，自检' + $(if ($rcSelf -eq 0) { '通过' } elseif ($rcSelf -lt 0) { '没做完' } else { "$rcSelf 项没过" }) +
     "，截图 $shots / $($Pages.Count)，退出" + $(if ($exitOk -and $forced -eq 0) { '正常' } else { '有问题' })) $(if ($ok) { 'Green' } else { 'Yellow' })
Say '告诉 Claude "验证做完了"就行，结果它自己读（验证结果\ 和几个 -结果.txt）。'
exit $(if ($ok) { 0 } else { 1 })
