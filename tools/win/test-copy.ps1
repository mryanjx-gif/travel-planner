# 0.4.1：帮 PO 在数据副本上测冲突、外部修改、恢复历史版本（不碰真实数据）。由 测试-准备数据副本.bat / 测试-模拟外部修改.bat 调用。
# 0.6.0 起原生版用自己的数据（travel.json），默认从开发版的数据文件夹 D:\旅行计划Native数据 复制。
#   prepare：把 D:\旅行计划Native数据 里的 travel.json、travel.backup.json、.history 复制到 D:\旅行计划Native数据-测试（不复制照片）
#   touch  ：在 D:\旅行计划Native数据-测试\travel.json 末尾加一个换行，相当于"别的程序改过它"（内容不变，文件仍然有效）
# PowerShell 5.1 需要本文件带 UTF-8 BOM。
param(
    [Parameter(Mandatory = $true)][ValidateSet('prepare', 'touch')][string]$Action,
    [string]$Source = 'D:\旅行计划Native数据',
    [string]$Target = 'D:\旅行计划Native数据-测试'
)
$ErrorActionPreference = 'Stop'
function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }

if ($Action -eq 'prepare') {
    if (-not (Test-Path (Join-Path $Source 'travel.json'))) { Say "[×] 没找到 $Source\travel.json（先在开发版里新建数据并从 Electron 版导入）" 'Red'; exit 1 }
    if (Test-Path (Join-Path $Target 'travel.json')) {
        Say "$Target 已经有 travel.json，用旧的副本继续测（要重新复制就先把这个文件夹移到【删除】里）。" 'Yellow'
        exit 0
    }
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    foreach ($f in 'travel.json', 'travel.backup.json') {
        $p = Join-Path $Source $f
        if (Test-Path $p) { Copy-Item $p $Target -Force }
    }
    $h = Join-Path $Source '.history'
    if (Test-Path $h) { Copy-Item $h (Join-Path $Target '.history') -Recurse -Force }
    New-Item -ItemType Directory -Force -Path (Join-Path $Target 'photos') | Out-Null
    $n = @(Get-ChildItem (Join-Path $Target '.history') -File -Force -ErrorAction SilentlyContinue).Count
    Say "已复制到 $Target（travel.json、travel.backup.json、.history 里 $n 份）。照片没有复制。" 'Green'
    exit 0
}

# touch
$file = Join-Path $Target 'travel.json'
if (-not (Test-Path $file)) { Say "[×] 没找到 $file，先运行 测试-准备数据副本.bat" 'Red'; exit 1 }
$fs = [System.IO.File]::Open($file, 'Append', 'Write')
try { $fs.WriteByte(10) } finally { $fs.Close() }
Say "已在 $file 末尾加了一个换行（模拟别的程序改过它）。3 秒内程序右下角应该出现「数据在别处更新了」。" 'Green'
exit 0
