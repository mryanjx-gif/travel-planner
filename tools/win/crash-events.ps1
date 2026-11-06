# 查崩溃记录（0.5.2 诊断用，只读）：由 查崩溃记录.bat 调用。
#   1) Windows"应用程序"日志里最近 3 天 TravelPlanner.exe 的事件：1000 程序出错、1001 Windows 错误报告、1002 程序卡住
#   2) Windows 错误报告（WER）文件夹里和 TravelPlanner 有关的报告（Report.wer 原文）
# 结果写到源码根目录的 查崩溃记录-结果.txt（Claude 自己读）。不改任何东西。
# PowerShell 5.1 需要本文件带 UTF-8 BOM。
$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # tools\win → 源码根目录
$Out = Join-Path $Root '查崩溃记录-结果.txt'
$sb = New-Object System.Text.StringBuilder
function W([string]$m = '') { [void]$sb.AppendLine($m); Write-Host $m }

W "查崩溃记录  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
W "系统：$([Environment]::OSVersion.VersionString)  PowerShell $($PSVersionTable.PSVersion)"
W ''

# ---------- 1. 事件日志 ----------
W '===== 1. 应用程序日志（最近 3 天，事件 1000 / 1001 / 1002，只列和 TravelPlanner 有关的）====='
$since = (Get-Date).AddDays(-3)
try {
    $evs = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000, 1001, 1002; StartTime = $since } -ErrorAction Stop
    $hit = @($evs | Where-Object { $_.Message -match 'TravelPlanner' -or (($_.Properties | ForEach-Object { "$($_.Value)" }) -join ' ') -match 'TravelPlanner' })
    W "共找到 $($evs.Count) 条 1000/1001/1002 事件，其中和 TravelPlanner 有关的 $($hit.Count) 条。"
    foreach ($e in ($hit | Sort-Object TimeCreated)) {
        W ''
        W "----- $($e.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss.fff'))  事件 $($e.Id)  来源 $($e.ProviderName)"
        $i = 0
        foreach ($p in $e.Properties) { W ("  [{0}] {1}" -f $i, $p.Value); $i++ }
        W '  消息：'
        foreach ($line in ($e.Message -split "`r?`n")) { if ($line.Trim()) { W "    $line" } }
    }
} catch {
    if ($_.Exception.Message -match 'No events were found|找不到') { W '最近 3 天没有 1000/1001/1002 事件。' }
    else { W "[!] 读不了事件日志：$($_.Exception.Message)" }
}
W ''

# ---------- 2. WER 报告 ----------
W '===== 2. Windows 错误报告文件夹（和 TravelPlanner 有关的，最近 3 天）====='
$werRoots = @(
    (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\WER\ReportArchive'),
    (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\WER\ReportQueue'),
    (Join-Path $env:ProgramData 'Microsoft\Windows\WER\ReportArchive'),
    (Join-Path $env:ProgramData 'Microsoft\Windows\WER\ReportQueue')
)
$found = 0
foreach ($r in $werRoots) {
    if (-not (Test-Path -LiteralPath $r)) { W "（没有这个文件夹：$r）"; continue }
    try {
        $dirs = @(Get-ChildItem -LiteralPath $r -Directory -ErrorAction Stop | Where-Object { $_.Name -match 'TravelPlanner' -and $_.LastWriteTime -ge $since } | Sort-Object LastWriteTime)
    } catch { W "[!] 读不了：$r（$($_.Exception.Message)）"; continue }
    foreach ($d in $dirs) {
        $found++
        W ''
        W "----- $($d.FullName)  ($($d.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')))"
        foreach ($f in (Get-ChildItem -LiteralPath $d.FullName -File -ErrorAction SilentlyContinue)) { W ("  文件：{0}  {1} 字节" -f $f.Name, $f.Length) }
        $wer = Join-Path $d.FullName 'Report.wer'
        if (Test-Path -LiteralPath $wer) {
            W '  Report.wer：'
            foreach ($line in (Get-Content -LiteralPath $wer -ErrorAction SilentlyContinue)) {
                if ($line -match '^(EventType|EventTime|ReportType|Consent|Sig\[|DynamicSig\[|AppName|AppPath|FriendlyEventName|ApplicationName|NsAppName|Response|ReportStatus|LoadedModule)') { W "    $line" }
            }
        }
    }
}
if ($found -eq 0) { W ''; W '没有找到和 TravelPlanner 有关的 WER 报告。' }
W ''
W '===== 完 ====='

$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($Out, $sb.ToString().Replace("`r`n", "`n"), $utf8)
Write-Host ''
Write-Host "已写到：$Out" -ForegroundColor Green
exit 0
