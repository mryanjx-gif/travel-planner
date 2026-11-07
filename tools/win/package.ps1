# 打包草稿（阶段 ⓪）：编 Release 版 → windeployqt 收集运行库 → 放两套地图库、省界数据、授权文本 → 打 zip，量大小并和 Electron 版比。
# 正式的 NSIS 安装程序在阶段 ⑧ 做。由 打包.bat 调用。PowerShell 5.1 需要本文件带 UTF-8 BOM。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Qt = 'D:\Qt\6.12.0\msvc2022_64'
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
# 开发用临时目录放在源码所在盘的英文路径（PO 2026-11-02：放 D 盘，不放 C 盘）
$Dev = (Split-Path $Root -Qualifier) + '\TravelPlannerNative-dev'
$Bld = Join-Path $Dev 'build-release'
$Ver = ((Select-String -Path (Join-Path $Root 'CMakeLists.txt') -Pattern 'set\(TP_VERSION "([^"]+)"\)').Matches[0].Groups[1].Value)
$Name = "TravelPlannerNative-$Ver"
$Stage = Join-Path $Root "dist\$Name"
$Zip = Join-Path $Root "dist\$Name-win64.zip"
$Result = Join-Path $Root '打包-结果.txt'
Set-Content -Path $Result -Value "打包草稿 $Ver  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Result -Value $m -Encoding UTF8 }
function Fail([string]$m) { Say "[×] $m" 'Red'; Say '请把 打包-结果.txt 发给 Claude。' 'Yellow'; exit 1 }
function MB([double]$b) { '{0:N1} MB' -f ($b / 1MB) }
function DirSize([string]$d) { (Get-ChildItem $d -Recurse -File -Force | Measure-Object Length -Sum).Sum }

# ---- VS 环境 ----
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail '没找到 VS 2022 Build Tools' }
foreach ($line in (cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set")) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

# ---- 1. 编 Release 版（源码经英文路径的目录联接：qmlimportscanner 读不了中文路径）----
Say "[1/5] 编 Release 版（$Bld）…"
$SrcLink = Join-Path $Dev 'src'
if (Test-Path $SrcLink) { cmd /c rmdir "`"$SrcLink`"" | Out-Null }
cmd /c mklink /J "`"$SrcLink`"" "`"$Root`"" | Out-Null
if (-not (Test-Path (Join-Path $SrcLink 'CMakeLists.txt'))) { Fail "建立目录联接失败：$SrcLink → $Root" }
$ErrorActionPreference = 'Continue'
# 差异清单 #36：配置和编译固定在系统 ANSI 代码页，头文件依赖才记得住
try { [Console]::OutputEncoding = [Text.Encoding]::GetEncoding([int](Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP) } catch {}
& cmake -S $SrcLink -B $Bld -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt" 2>&1 | Out-File -Append -Encoding utf8 (Join-Path $Dev 'package.log')
if ($LASTEXITCODE -ne 0) { $ErrorActionPreference = 'Stop'; Fail '生成编译配置失败（日志 D:\TravelPlannerNative-dev\package.log）' }
& cmake --build $Bld 2>&1 | Out-File -Append -Encoding utf8 (Join-Path $Dev 'package.log')
if ($LASTEXITCODE -ne 0) { $ErrorActionPreference = 'Stop'; Fail '编译失败（日志 D:\TravelPlannerNative-dev\package.log）' }
$ErrorActionPreference = 'Stop'

# ---- 2. 收集文件 ----
Say "[2/5] 收集到 dist\$Name\ …"
if (Test-Path $Stage) {
    $old = Join-Path $Root ('【删除】\dist-旧-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path (Split-Path $old) | Out-Null
    Move-Item $Stage $old
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item (Join-Path $Bld 'TravelPlanner.exe') $Stage
Copy-Item (Join-Path $Bld 'assets') $Stage -Recurse
foreach ($b in 'vulkan', 'opengl') {
    $src = Join-Path $Root "third_party\maplibre-native-qt\install-$b"
    if (-not (Test-Path (Join-Path $src 'qml\MapLibre\qmldir'))) { Say "  [!] 没有 $b 版地图库，跳过" 'Yellow'; continue }
    $dst = Join-Path $Stage "maplibre\$b"
    New-Item -ItemType Directory -Force -Path (Join-Path $dst 'bin') | Out-Null
    Copy-Item (Join-Path $src 'bin\*.dll') (Join-Path $dst 'bin')
    Copy-Item (Join-Path $src 'qml') $dst -Recurse
}
# windeployqt：只带用得到的；qmltooling（调试用）、不用的图片格式插件不带
$wdq = Join-Path $Qt 'bin\windeployqt.exe'
# --no-system-dxc-compiler：不带 dxcompiler.dll / dxil.dll（Direct3D 的着色器编译器，15 MB；我们只用 Vulkan / OpenGL）
$common = @('--release', '--no-translations', '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--no-opengl-sw', '--no-compiler-runtime',
            '--skip-plugin-types', 'qmltooling,generic',
            '--exclude-plugins', 'qtiff,qicns,qtga,qwbmp,qpdf')   # 0.7.0 起要 qwebp（WebP 照片；libwebp BSD-3，授权在阶段 ⑧ 列进"关于"）
& $wdq @common --qmldir (Join-Path $SrcLink 'qml') (Join-Path $Stage 'TravelPlanner.exe') *> (Join-Path $Dev 'windeployqt-release.log')
if ($LASTEXITCODE -ne 0) { Fail 'windeployqt 出错（日志 D:\TravelPlannerNative-dev\windeployqt-release.log）' }
Get-ChildItem (Join-Path $Stage 'maplibre') -Recurse -Filter *.dll | ForEach-Object {
    & $wdq @common --no-quick-import --dir $Stage $_.FullName *>> (Join-Path $Dev 'windeployqt-release.log')
}
# VC++ 运行库：直接放 DLL（程序目录本地部署，不带 vc_redist 安装包）
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if ($crt) { Copy-Item (Join-Path $crt.FullName '*.dll') $Stage; Say "  VC++ 运行库：$($crt.Name)" } else { Say '  [!] 没找到 VC++ 运行库 DLL' 'Yellow' }
# 授权文本（草稿：maplibre 全套 + Qt LGPL；完整清单在阶段 ⑧ 按实际文件核对）
$lic = Join-Path $Stage 'LICENSES'
New-Item -ItemType Directory -Force -Path (Join-Path $lic 'maplibre-native-qt') | Out-Null
Copy-Item (Join-Path $Root 'third_party\maplibre-native-qt\LICENSES\*') (Join-Path $lic 'maplibre-native-qt')
Copy-Item (Join-Path $Root 'assets\fonts\LICENSE-*.txt'), (Join-Path $Root 'assets\licenses\LICENSE-suncalc.txt') $lic
$qtLic = Join-Path (Split-Path (Split-Path $Qt)) 'Licenses'
if (Test-Path $qtLic) { Copy-Item $qtLic (Join-Path $lic 'Qt') -Recurse } else { Say "  [!] 没找到 Qt 授权文本目录 $qtLic（阶段 ⑧ 补）" 'Yellow' }
Get-ChildItem $Stage -Recurse -Include *.pdb, *.ilk | Remove-Item -Force

# ---- 3. 检查不该带的 ----
Say '[3/5] 检查…'
foreach ($bad in 'Qt6ShaderTools.dll', 'Qt6QmlCompiler.dll', 'Qt6Quick3D.dll', 'Qt6WebEngineCore.dll') {
    if (Test-Path (Join-Path $Stage $bad)) { Say "  [!] 带上了不该带的 $bad" 'Yellow' }
}
if (Test-Path (Join-Path $Stage 'Qt6QuickEffects.dll')) { Say '  [i] 带了 Qt6QuickEffects.dll（LGPL-3，已核实可用）' }

# ---- 4. zip ----
Say '[4/5] 打 zip…'
if (Test-Path $Zip) { Move-Item $Zip (Join-Path $Root ('【删除】\' + (Split-Path $Zip -Leaf) + '.' + (Get-Date -Format 'HHmmss'))) }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($Stage, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)

# ---- 5. 大小对比 ----
Say '[5/5] 大小对比：'
$dirSize = DirSize $Stage
$zipSize = (Get-Item $Zip).Length
$fileCount = (Get-ChildItem $Stage -Recurse -File).Count
Say "  原生 $Ver：安装后约 $(MB $dirSize)（$fileCount 个文件），zip $(MB $zipSize)"
$eSetup = Get-ChildItem 'D:\旅行计划App\dist' -Filter 'TravelPlanner-Setup-6.3.0.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($eSetup) { Say "  Electron v6.3.0：安装包 $(MB $eSetup.Length)" }
$eUnp = 'D:\旅行计划App\dist\win-unpacked'
if (Test-Path $eUnp) { Say "  Electron（dist\win-unpacked，最近一次打包）：安装后约 $(MB (DirSize $eUnp))" }
Say '  注：zip 是 Deflate 压缩；NSIS 安装包用 LZMA，通常比 zip 再小 20-30%（估计）。'
Say ''
Say '  最大的 15 个文件：'
Get-ChildItem $Stage -Recurse -File | Sort-Object Length -Descending | Select-Object -First 15 | ForEach-Object {
    Say ('    {0,10}  {1}' -f (MB $_.Length), $_.FullName.Substring($Stage.Length + 1))
}
Say ''
Say '  按类别：'
$m1 = DirSize (Join-Path $Stage 'maplibre'); Say ('    {0,10}  地图库 maplibre（两套）' -f (MB $m1))
$a1 = DirSize (Join-Path $Stage 'assets'); Say ('    {0,10}  省界数据 assets' -f (MB $a1))
$top = (Get-ChildItem $Stage -File | Measure-Object Length -Sum).Sum; Say ('    {0,10}  程序目录根部（exe + Qt 和 VC++ DLL）' -f (MB $top))
$rest = $dirSize - $m1 - $a1 - $top; Say ('    {0,10}  其余（Qt 插件、QML 模块、授权文本）' -f (MB $rest))
Say ''
Say "完成：$Zip" 'Green'
exit 0
