# 准备 maplibre-native-qt 源码：合并分块 → 校验 SHA-256 → 解压到 src\（跳过路径超长的测试文件）→ 分块移进【删除】
# 由 准备maplibre源码.bat 调用。PowerShell 5.1 需要本文件带 UTF-8 BOM。
param([Parameter(Mandatory = $true)][string]$Root)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
$Mln = Join-Path $Root 'third_party\maplibre-native-qt'
$Pkg = Join-Path $Mln '源码包'
$Src = Join-Path $Mln 'src'
$Del = Join-Path $Root '【删除】\maplibre源码分块'
$ZipName = 'maplibre-native-qt-399311e.zip'
$Zip = Join-Path $Pkg $ZipName
$Log = Join-Path $Root '准备maplibre源码-结果.txt'
$Marker = Join-Path $Src '.解压完成.txt'
$t0 = Get-Date
Set-Content -Path $Log -Value "准备 maplibre 源码  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Log -Value $m -Encoding UTF8 }
function Fail([string]$m) { Say "[×] $m" 'Red'; Say '请把 准备maplibre源码-结果.txt 发给 Claude。' 'Yellow'; exit 1 }
function Elapsed { $s = [int]((Get-Date) - $t0).TotalSeconds; '{0} 分 {1:00} 秒' -f [math]::Floor($s / 60), ($s % 60) }

# ---- 读校验表 ----
$sumFile = Join-Path $Pkg 'SHA256SUMS.txt'
if (-not (Test-Path $sumFile)) { Fail "缺少 $sumFile" }
$sums = @{}
foreach ($line in Get-Content $sumFile) {
    if ($line -match '^([0-9a-f]{64}) \*(.+)$') { $sums[$Matches[2]] = $Matches[1].ToUpper() }
}
$wantZip = $sums[$ZipName]
$parts = $sums.Keys | Where-Object { $_ -like "$ZipName.*" } | Sort-Object
Say "源码包：$ZipName（$($parts.Count) 块），目标 SHA-256 $wantZip"

# ---- 已经解压过就跳过 ----
if ((Test-Path $Marker) -and ((Get-Content $Marker -Raw) -match $wantZip)) {
    Say "[√] src\ 已经解压过（$Marker），不用再做。" 'Green'
    exit 0
}

# ---- 磁盘空间 ----
$drive = (Get-Item $Root).PSDrive
$freeGB = [math]::Round($drive.Free / 1GB, 1)
Say "[i] $($drive.Name): 盘剩余 $freeGB GB（解压约需 1 GB，编译两套约需 12 GB）"
if ($freeGB -lt 3) { Fail '剩余空间不足 3 GB' }

# ---- 1. 合并（整份 zip 已存在且校验通过就不再合并）----
$haveZip = $false
if (Test-Path $Zip) {
    Say '[1/4] 源码包已合并过，校验中…'
    if ((Get-FileHash $Zip -Algorithm SHA256).Hash -eq $wantZip) { $haveZip = $true; Say '      [√] 整份 zip 校验通过' 'Green' }
    else { Say '      整份 zip 校验不对，重新合并' 'Yellow'; Remove-Item $Zip -Force }
}
if (-not $haveZip) {
    Say '[1/4] 校验各分块…'
    foreach ($p in $parts) {
        $f = Join-Path $Pkg $p
        if (-not (Test-Path $f)) { Fail "缺少分块 $p" }
        $h = (Get-FileHash $f -Algorithm SHA256).Hash
        if ($h -ne $sums[$p]) { Fail "分块 $p 校验不对（实际 $h）" }
    }
    Say "      [√] $($parts.Count) 块全部校验通过" 'Green'
    Say '      合并中…'
    $out = [System.IO.File]::Create($Zip)
    try {
        foreach ($p in $parts) {
            $in = [System.IO.File]::OpenRead((Join-Path $Pkg $p))
            try { $in.CopyTo($out) } finally { $in.Dispose() }
        }
    } finally { $out.Dispose() }
    $h = (Get-FileHash $Zip -Algorithm SHA256).Hash
    if ($h -ne $wantZip) { Fail "合并后的 zip 校验不对（实际 $h）" }
    Say "      [√] 合并完成，整份 zip 校验通过（$([math]::Round((Get-Item $Zip).Length / 1MB)) MB）" 'Green'
}

# ---- 2. 解压（跳过 Windows 路径超长的文件）----
Say "[2/4] 解压到 $Src …（约 8.7 万个文件，几分钟；杀毒软件可能让它更慢）"
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path $Src) {
    $old = Join-Path $Root ('【删除】\maplibre-src-旧-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path (Split-Path $old) | Out-Null
    Move-Item $Src $old
    Say "      旧的 src\ 移到了 $old" 'Yellow'
}
New-Item -ItemType Directory -Force -Path $Src | Out-Null
$skipped = New-Object System.Collections.Generic.List[string]
$archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $n = $archive.Entries.Count; $i = 0; $done = 0
    foreach ($e in $archive.Entries) {
        $i++
        if ($e.FullName.EndsWith('/')) { continue }
        $rel = $e.FullName.Replace('/', '\')
        $target = [System.IO.Path]::Combine($Src, $rel)
        $dir = [System.IO.Path]::GetDirectoryName($target)
        if ($target.Length -gt 259 -or $dir.Length -gt 247) { $skipped.Add($e.FullName); continue }
        if (-not [System.IO.Directory]::Exists($dir)) { [System.IO.Directory]::CreateDirectory($dir) | Out-Null }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $target, $true)
        $done++
        if ($i % 2000 -eq 0) {
            $pct = [int](100 * $i / $n)
            Write-Progress -Activity '解压 maplibre 源码' -Status "$i / $n（$pct%）  已用 $(Elapsed)" -PercentComplete $pct
        }
    }
} finally { $archive.Dispose(); Write-Progress -Activity '解压 maplibre 源码' -Completed }
$skipFile = Join-Path $Src '未解压的文件.txt'
$hdr = @("# 这些文件在 $Src 下的完整路径超过 Windows 的 260 字符上限，没有解压（都在 zip 里，原样保留）。",
         "# 全部是测试数据，编译用不到。共 $($skipped.Count) 个。", '')
Set-Content -Path $skipFile -Value ($hdr + $skipped) -Encoding UTF8
$notTest = @($skipped | Where-Object { $_ -notmatch '/tests?/' })
Say "      [√] 解压了 $done 个文件；跳过 $($skipped.Count) 个超长路径文件（清单：src\未解压的文件.txt）" 'Green'
if ($notTest.Count -gt 0) { Say "      [!] 其中 $($notTest.Count) 个不在 test 目录里，第一个：$($notTest[0])" 'Yellow' }
foreach ($must in @('CMakeLists.txt', 'src\quick\CMakeLists.txt', 'vendor\maplibre-native\CMakeLists.txt', 'vendor\maplibre-native\vendor\Vulkan-Headers\include\vulkan\vulkan.h')) {
    if (-not (Test-Path (Join-Path $Src $must))) { Fail "解压后缺少 $must" }
}
Set-Content -Path $Marker -Value "zip SHA-256 $wantZip`r`n解压时间 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`r`n文件 $done，跳过 $($skipped.Count)" -Encoding UTF8

# ---- 3. 授权文本 ----
Say '[3/4] 授权文本…'
$lic = Join-Path $Mln 'LICENSES'
foreach ($f in @('BSD-2-Clause.txt', 'LGPL-3.0-only.txt', 'maplibre-native-LICENSE.md', 'maplibre-native-LICENSES.core.md')) {
    if (-not (Test-Path (Join-Path $lic $f))) { Fail "缺少授权文本 LICENSES\$f" }
}
Say '      [√] third_party\maplibre-native-qt\LICENSES\ 齐全' 'Green'

# ---- 4. 分块和传输测试文件移进【删除】----
Say '[4/4] 分块移进【删除】（整份 zip 留在 源码包\）…'
New-Item -ItemType Directory -Force -Path $Del | Out-Null
foreach ($p in $parts) { $f = Join-Path $Pkg $p; if (Test-Path $f) { Move-Item $f $Del -Force } }
Get-ChildItem $Pkg -Filter '_t*' -ErrorAction SilentlyContinue | ForEach-Object { Move-Item $_.FullName $Del -Force }
Say "      [√] 已移到 $Del（可以自己删掉）" 'Green'

# ---- 附：测试数据副本（只读复制，不改原文件）----
$td = Join-Path $Root 'testdata'
$tdFile = Join-Path $td 'data.js'
$real = 'D:\旅行计划数据\data.js'
if (-not (Test-Path $tdFile)) {
    if (Test-Path $real) {
        New-Item -ItemType Directory -Force -Path $td | Out-Null
        Copy-Item $real $tdFile
        Say "[附] 复制了测试数据副本：testdata\data.js（来源 $real，只读复制）" 'Green'
    } else { Say "[附] 没找到 $real，testdata\data.js 需要你手动复制一份" 'Yellow' }
} else { Say '[附] testdata\data.js 已存在，不覆盖' }
if (Test-Path $tdFile) { Say "      testdata\data.js MD5 $((Get-FileHash $tdFile -Algorithm MD5).Hash)  $((Get-Item $tdFile).Length) 字节" }

Say ''
Say "完成，用时 $(Elapsed)。" 'Green'
exit 0
