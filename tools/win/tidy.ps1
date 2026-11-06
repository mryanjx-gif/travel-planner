# 整理根目录：把用不到的旧文件移进【删除】\旧文件-日期-时间\（只移动，不删除；要不要删由 PO 决定）。由 整理文件.bat 调用。
#   - 各个 .bat 生成的"…-结果.txt"（下次运行对应的 .bat 会重新生成）
#   - 编译maplibre-日志\、内存测试-结果\（结论已写进 claude\v7.0.0-b记录.md，对比图在 claude\ 里）
#   - dist\ 里阶段 ⓪ 的旧原型包 TravelPlannerNative-0.2.2（文件夹和 zip）
#   - 整理文件.bat 自己（用完就不需要了）
# PowerShell 5.1 需要本文件带 UTF-8 BOM。
$ErrorActionPreference = 'Stop'
function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # tools\win → 源码根目录
$Dest = Join-Path $Root ('【删除】\旧文件-' + (Get-Date -Format 'yyyyMMdd-HHmm'))

$items = @()
$items += Get-ChildItem -LiteralPath $Root -File -Filter '*-结果.txt' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }
foreach ($n in '编译maplibre-日志', '内存测试-结果', 'dist\TravelPlannerNative-0.2.2', 'dist\TravelPlannerNative-0.2.2-win64.zip', '整理文件.bat') {
    if (Test-Path -LiteralPath (Join-Path $Root $n)) { $items += $n }
}
if ($items.Count -eq 0) { Say '没有要移走的文件。' 'Green'; exit 0 }

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$ok = 0; $fail = 0
foreach ($n in $items) {
    $src = Join-Path $Root $n
    $to = Join-Path $Dest $n
    try {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Move-Item -LiteralPath $src -Destination $to
        Say "  已移走：$n" ; $ok++
    } catch {
        Say "  [!] 没移动：$n（$($_.Exception.Message)）" 'Yellow'; $fail++
    }
}
Say ''
Say "完成：移走 $ok 项，放在 $Dest" 'Green'
if ($fail -gt 0) { Say "有 $fail 项没移动（可能正被别的程序打开），关掉后再运行一次就行。" 'Yellow' }
exit 0
