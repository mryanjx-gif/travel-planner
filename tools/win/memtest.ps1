# 内存测试：自动依次跑 Vulkan、OpenGL 各 N 分钟（默认 60），每 30 秒从外面采样一次进程内存和显存，最后画对比图。
# 可选：先对已经打开的 Electron 版只读采样 10 分钟作参照（不操作、不改 Electron 任何东西）。
# -Mode steps（0.8.6，内存分步测试.bat）：只测 Vulkan，连跑 4 轮（全部步骤 / 不进地球 / 不开已解锁 / 不用卫星图），每轮 -StepMinutes 分钟，
#   程序每一步开始前自己记一行内存和显存，最后算出"每一步涨了多少"和"每轮最后稳定在多少"。
# 由 内存测试.bat / 内存分步测试.bat 调用。PowerShell 5.1 需要本文件带 UTF-8 BOM。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Build = '',
    [int]$Minutes = 60,
    [int]$ElectronMinutes = 10,
    [int]$Interval = 30,
    [ValidateSet('full', 'steps')][string]$Mode = 'full',
    [int]$StepMinutes = 4
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
if (-not $Build) { $Build = (Split-Path $Root -Qualifier) + '\TravelPlannerNative-dev\app' }
$Exe = Join-Path $Build 'TravelPlanner.exe'
$Out = Join-Path $Root '内存测试-结果'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Stamp = Get-Date -Format 'yyyyMMdd-HHmm'
$Summary = Join-Path $Out "结果-$Stamp.txt"
Set-Content -Path $Summary -Value "内存测试  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  每 $Interval 秒采样" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Summary -Value $m -Encoding UTF8 }
function Fail([string]$m) { Say "[×] $m" 'Red'; Say "请把 内存测试-结果\ 文件夹里最新的文件发给 Claude。" 'Yellow'; exit 1 }

# 测试期间不让电脑睡眠、不关屏幕（屏幕关了显卡可能停止刷新，测不准）
Add-Type -Namespace TP -Name Power -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);'
[void][TP.Power]::SetThreadExecutionState([uint32]'0x80000003')

if (-not (Test-Path $Exe)) { Fail "没找到 $Exe：先运行 编译.bat" }
foreach ($b in 'vulkan', 'opengl') {
    if (-not (Test-Path (Join-Path $Build "maplibre\$b\qml\MapLibre\qmldir"))) { Say "[!] 程序目录里没有 $b 版地图库，这一项会失败" 'Yellow' }
}
if (-not (Test-Path (Join-Path $Root 'testdata\travel.json'))) { Fail '没有 testdata\travel.json（0.6.0 起内存测试用原生格式的测试数据）' }

# 显存：WMI 的 GPU 进程内存计数器（类名不随系统语言变化）；拿不到就留空
function Get-GpuMB([int[]]$pids) {
    try {
        $rows = Get-CimInstance -ClassName Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -ErrorAction Stop
        $ded = 0.0; $sh = 0.0; $hit = $false
        foreach ($r in $rows) {
            foreach ($p in $pids) {
                if ($r.Name -like "pid_$($p)_*") { $ded += [double]$r.DedicatedUsage; $sh += [double]$r.SharedUsage; $hit = $true }
            }
        }
        if ($hit) { return @([math]::Round($ded / 1MB, 1), [math]::Round($sh / 1MB, 1)) }
    } catch {}
    return @('', '')
}
function Sample([string]$csv, [string]$label, [double]$t, [System.Diagnostics.Process[]]$procs) {
    $ws = 0.0; $pv = 0.0
    foreach ($p in $procs) { try { $p.Refresh(); $ws += $p.WorkingSet64; $pv += $p.PrivateMemorySize64 } catch {} }
    $g = Get-GpuMB ($procs | ForEach-Object { $_.Id })
    $line = '{0},{1},{2},{3},{4},{5}' -f [int]$t, $label, [math]::Round($ws / 1MB, 1), [math]::Round($pv / 1MB, 1), $g[0], $g[1]
    Add-Content -Path $csv -Value $line -Encoding UTF8
    return $line
}
$Header = 'elapsed_s,label,working_set_mb,private_mb,gpu_dedicated_mb,gpu_shared_mb'
$csvs = @()

function Median([double[]]$v) {
    if (-not $v -or $v.Count -eq 0) { return [double]::NaN }
    $s = @($v | Sort-Object)
    if ($s.Count % 2) { return $s[($s.Count - 1) / 2] }
    return ($s[$s.Count / 2 - 1] + $s[$s.Count / 2]) / 2
}
function Fmt([double]$x) { if ([double]::IsNaN($x)) { return '—' } return ('{0:0}' -f $x) }
function FmtD([double]$x) { if ([double]::IsNaN($x)) { return '—' } if ($x -ge 0) { return ('+{0:0}' -f $x) } return ('{0:0}' -f $x) }

# ---- 分步模式（0.8.6）----
if ($Mode -eq 'steps') {
    $Interval = 10
    $runs = @(
        @{ Name = '全部步骤'; Skip = '' },
        @{ Name = '不进地球'; Skip = 'globe' },
        @{ Name = '不开已解锁'; Skip = 'unlock' },
        @{ Name = '不用卫星图'; Skip = 'sat' }
    )
    $total = $runs.Count * ($StepMinutes + 0.3)
    Say ("==== 内存分步测试：原生版 Vulkan，{0} 轮 × {1} 分钟，约 {2:0} 分钟 ====" -f $runs.Count, $StepMinutes, $total) 'Cyan'
    Say '  不要最小化或遮挡程序窗口，也不要点它。'
    $stepCsvs = @{}
    $extCsvs = @{}
    $k = 0
    foreach ($r in $runs) {
        $k++
        $name = $r.Name
        $csv = Join-Path $Out "steps-ext-$k-$Stamp.csv"; Set-Content -Path $csv -Value $Header -Encoding UTF8; $csvs += $csv; $extCsvs[$name] = $csv
        $scsv = Join-Path $Out "steps-$k-$Stamp.csv"; $stepCsvs[$name] = $scsv
        $log = Join-Path $Out "steps-$k-$Stamp.log"
        $a = @('--backend=vulkan', '--memtest', "$StepMinutes", '--memsteps', ('"' + $scsv + '"'), '--label', $name)
        if ($r.Skip) { $a += @('--memtest-skip', $r.Skip) }
        Say "---- 第 $k/$($runs.Count) 轮：$name ----"
        $p = Start-Process -FilePath $Exe -ArgumentList $a -WorkingDirectory $Build -PassThru -RedirectStandardError $log
        $null = $p.Handle   # PowerShell 5.1：不先取一次句柄，退出后 ExitCode 可能是空的（Windows差异清单 #12）
        $t0 = Get-Date
        while (-not $p.HasExited) {
            $t = ((Get-Date) - $t0).TotalSeconds
            if ($t -gt 5) { [void](Sample $csv $name $t @($p)) }
            if ($t / 60 -gt $StepMinutes + 3) { Say '  超时，结束进程' 'Yellow'; Stop-Process -Id $p.Id -Force; break }
            $left = [math]::Max(0, $StepMinutes - $t / 60) + ($runs.Count - $k) * $StepMinutes
            Write-Progress -Activity "内存分步测试：$name（第 $k/$($runs.Count) 轮）" -Status ("全部还剩约 {0:0} 分" -f $left) -PercentComplete ([math]::Min(100, [int](100 * $t / 60 / $StepMinutes)))
            Start-Sleep -Seconds $Interval
        }
        Write-Progress -Activity '内存分步测试' -Completed
        $used = ((Get-Date) - $t0).TotalMinutes
        if ($used -lt 1) { Say ("  [!] {0:0.0} 分钟就退出了（退出码 {1}），可能启动失败；日志 $log" -f $used, $p.ExitCode) 'Yellow' }
        elseif (-not (Test-Path $scsv)) { Say '  [!] 程序没写出分步记录（是不是还没运行 编译.bat？）' 'Yellow' }
        else { Say ("  [√] 完成，{0:0.0} 分钟" -f $used) 'Green' }
    }
    [void][TP.Power]::SetThreadExecutionState([uint32]'0x80000000')

    $png = Join-Path $Out "内存分步-$Stamp.png"
    $plotArgs = @('--plot', ('"' + $png + '"')) + ($csvs | ForEach-Object { '"' + $_ + '"' })
    $pp = Start-Process -FilePath $Exe -ArgumentList $plotArgs -WorkingDirectory $Build -PassThru -Wait
    if (Test-Path $png) { Say "[√] 曲线图：$png" 'Green' } else { Say "[!] 曲线图没生成（退出码 $($pp.ExitCode)）" 'Yellow' }

    # 表 1：每轮最后 1 分钟的中位数（私有内存：外部采样；显存：程序自己读的 DXGI 和外部 WMI 共享显存）
    Say ''
    Say '表 1：每轮最后 1 分钟稳定在多少（MB，中位数）'
    Say '  轮次 | 私有内存 | 和"全部步骤"比 | 显存 DXGI 本地+借用 | WMI 共享显存'
    $base = [double]::NaN
    foreach ($r in $runs) {
        $name = $r.Name
        $ext = @(Import-Csv $extCsvs[$name])
        $pv = [double]::NaN; $sh = [double]::NaN; $gd = [double]::NaN
        if ($ext.Count -gt 0) {
            $tEnd = [double]$ext[-1].elapsed_s
            $tail = @($ext | Where-Object { [double]$_.elapsed_s -ge $tEnd - 60 })
            $pv = Median @($tail | ForEach-Object { [double]$_.private_mb })
            $shv = @($tail | Where-Object { $_.gpu_shared_mb -ne '' } | ForEach-Object { [double]$_.gpu_shared_mb })
            if ($shv.Count) { $sh = Median $shv }
        }
        if (Test-Path $stepCsvs[$name]) {
            $st = @(Import-Csv $stepCsvs[$name])
            if ($st.Count -gt 0) {
                $tEnd = [double]$st[-1].elapsed_s
                $g = @($st | Where-Object { [double]$_.elapsed_s -ge $tEnd - 60 -and $_.gpu_local_mb -ne '' } | ForEach-Object { [double]$_.gpu_local_mb + [double]$_.gpu_nonlocal_mb })
                if ($g.Count) { $gd = Median $g }
            }
        }
        if ($r.Skip -eq '') { $base = $pv }
        $diff = if ($r.Skip -eq '') { '—' } else { FmtD ($pv - $base) }
        Say ("  {0} | {1} | {2} | {3} | {4}" -f $name, (Fmt $pv), $diff, (Fmt $gd), (Fmt $sh))
    }

    # 表 2："全部步骤"那一轮里，每一步做完以后涨了多少（第 1、2、3 圈）
    $full = $stepCsvs[$runs[0].Name]
    if (Test-Path $full) {
        $st = @(Import-Csv $full)
        Say ''
        Say '表 2："全部步骤"一轮里每一步做完 6 秒后涨了多少（MB；私有内存 / 显存 DXGI 本地+借用）'
        Say '  步骤 | 第 1 圈 | 第 2 圈 | 第 3 圈'
        $rows = @{}
        $order = @()
        for ($i = 1; $i -lt $st.Count; $i++) {
            $loop = [int]$st[$i].loop
            if ($loop -gt 3) { break }
            $dp = [double]$st[$i].private_mb - [double]$st[$i - 1].private_mb
            $dg = if ($st[$i].gpu_local_mb -ne '' -and $st[$i - 1].gpu_local_mb -ne '') { ([double]$st[$i].gpu_local_mb + [double]$st[$i].gpu_nonlocal_mb) - ([double]$st[$i - 1].gpu_local_mb + [double]$st[$i - 1].gpu_nonlocal_mb) } else { [double]::NaN }
            $key = $st[$i].after_step
            if (-not $rows.ContainsKey($key)) { $rows[$key] = @{}; $order += $key }
            # 第 0 圈的最后一行（loop 1 开头记的）属于第 1 圈做的最后一步，按"做这一步时是第几圈"归类
            $doneLoop = [int]$st[$i - 1].loop
            $rows[$key][$doneLoop] = '{0} / {1}' -f (FmtD $dp), (FmtD $dg)
        }
        foreach ($key in $order) {
            $c = @(0, 1, 2) | ForEach-Object { if ($rows[$key].ContainsKey($_)) { $rows[$key][$_] } else { '—' } }
            Say ("  {0} | {1} | {2} | {3}" -f $key, $c[0], $c[1], $c[2])
        }
        if ($st.Count -gt 0) { Say ("  启动后第一次记录：私有 {0} MB，显存 {1} MB" -f $st[0].private_mb, $(if ($st[0].gpu_local_mb -ne '') { [double]$st[0].gpu_local_mb + [double]$st[0].gpu_nonlocal_mb } else { '—' })) }
    }
    Say ''
    Say "全部完成。告诉 Claude 跑完了（它会读 内存测试-结果\ 里这次的 .txt、.csv、.png）。" 'Green'
    exit 0
}

# ---- 0. Electron 参照（可选）----
$ours = (Resolve-Path $Build).Path
$el = @(Get-Process -Name TravelPlanner -ErrorAction SilentlyContinue | Where-Object { $_.Path -and -not $_.Path.StartsWith($ours, [StringComparison]::OrdinalIgnoreCase) })
if ($el.Count -gt 0) {
    $ver = try { (Get-Item $el[0].Path).VersionInfo.ProductVersion } catch { '?' }
    $label = "Electron $ver（参照）"
    $csv = Join-Path $Out "electron-$Stamp.csv"; Set-Content -Path $csv -Value $Header -Encoding UTF8; $csvs += $csv
    Say "==== Electron 参照：$($el.Count) 个进程（$($el[0].Path)），只读采样 $ElectronMinutes 分钟 ====" 'Cyan'
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalMinutes -lt $ElectronMinutes) {
        $el = @(Get-Process -Name TravelPlanner -ErrorAction SilentlyContinue | Where-Object { $_.Path -and -not $_.Path.StartsWith($ours, [StringComparison]::OrdinalIgnoreCase) })
        if ($el.Count -eq 0) { Say '  Electron 被关掉了，参照提前结束' 'Yellow'; break }
        $t = ((Get-Date) - $t0).TotalSeconds
        [void](Sample $csv $label $t $el)
        Write-Progress -Activity 'Electron 参照' -Status ("已用 {0:0} 分，还剩 {1:0} 分" -f ($t / 60), ($ElectronMinutes - $t / 60)) -PercentComplete ([int](100 * $t / 60 / $ElectronMinutes))
        Start-Sleep -Seconds $Interval
    }
    Write-Progress -Activity 'Electron 参照' -Completed
    Say '  Electron 参照完成（Electron 窗口可以关掉了；不关也行，它停在地图页基本不占资源）'
} else {
    Say '[i] 没有检测到打开着的 Electron 版，跳过参照'
}

# ---- 1、2. 原生版：Vulkan、OpenGL ----
$n = 0
foreach ($b in 'vulkan', 'opengl') {
    $n++
    $Disp = if ($b -eq 'vulkan') { 'Vulkan' } else { 'OpenGL' }
    $label = "原生 $Disp"
    $csv = Join-Path $Out "$b-$Stamp.csv"; Set-Content -Path $csv -Value $Header -Encoding UTF8; $csvs += $csv
    $log = Join-Path $Out "$b-$Stamp.log"
    Say "==== 第 $n/2 项：原生版 $Disp，$Minutes 分钟（程序自己循环：换底图、平移缩放、地球、玻璃面板）====" 'Cyan'
    Say '  不要最小化或遮挡程序窗口，也不要点它。'
    $p = Start-Process -FilePath $Exe -ArgumentList "--backend=$b", "--memtest", "$Minutes" -WorkingDirectory $Build -PassThru -RedirectStandardError $log
    $t0 = Get-Date
    $deadline = $Minutes + 3
    while (-not $p.HasExited) {
        $t = ((Get-Date) - $t0).TotalSeconds
        if ($t -gt 5) { [void](Sample $csv $label $t @($p)) }
        if ($t / 60 -gt $deadline) { Say "  超时（$deadline 分钟），结束进程" 'Yellow'; Stop-Process -Id $p.Id -Force; break }
        $rem = [math]::Max(0, $Minutes - $t / 60) + ($(if ($b -eq 'vulkan') { $Minutes } else { 0 }))
        Write-Progress -Activity "原生版 $Disp" -Status ("已用 {0:0} 分，这一项还剩 {1:0} 分，全部还剩约 {2:0} 分" -f ($t / 60), [math]::Max(0, $Minutes - $t / 60), $rem) -PercentComplete ([math]::Min(100, [int](100 * $t / 60 / $Minutes)))
        Start-Sleep -Seconds $Interval
    }
    Write-Progress -Activity "原生版 $Disp" -Completed
    $used = ((Get-Date) - $t0).TotalMinutes
    if ($used -lt 2) { Say ("  [!] $Disp 版 {0:0.0} 分钟就退出了（退出码 {1}），可能启动失败；日志 $log" -f $used, $p.ExitCode) 'Yellow' }
    else { Say ("  [√] $Disp 完成，{0:0} 分钟" -f $used) 'Green' }
}
[void][TP.Power]::SetThreadExecutionState([uint32]'0x80000000')

# ---- 3. 对比图 + 数字 ----
$png = Join-Path $Out "内存对比-$Stamp.png"
$plotArgs = @('--plot', ('"' + $png + '"')) + ($csvs | ForEach-Object { '"' + $_ + '"' })
$pp = Start-Process -FilePath $Exe -ArgumentList $plotArgs -WorkingDirectory $Build -PassThru -Wait
if (Test-Path $png) { Say "[√] 对比图：$png" 'Green'; Invoke-Item $png } else { Say "[!] 对比图没生成（退出码 $($pp.ExitCode)）" 'Yellow' }
Say ''
Say '数字摘要（私有内存，MB）：'
foreach ($c in $csvs) {
    $rows = @(Import-Csv $c)
    if ($rows.Count -lt 2) { Say "  $(Split-Path $c -Leaf)：数据太少"; continue }
    $pv = $rows | ForEach-Object { [double]$_.private_mb }
    $gd = @($rows | Where-Object { $_.gpu_dedicated_mb -ne '' } | ForEach-Object { [double]$_.gpu_dedicated_mb })
    $g = if ($gd.Count) { "；显存 起 $($gd[0]) → 末 $($gd[-1])，峰 $(($gd | Measure-Object -Maximum).Maximum)" } else { '' }
    Say ("  {0}：起 {1} → 末 {2}，峰 {3}{4}" -f $rows[0].label, $pv[0], $pv[-1], ($pv | Measure-Object -Maximum).Maximum, $g)
}
Say ''
Say "全部完成。请把 内存测试-结果\ 里这次的 .png、.txt、.csv 发给 Claude（或告诉我跑完了，我直接读）。" 'Green'
exit 0
