# 打包（0.14.2）：编 Release 版 → windeployqt 收集运行库（瘦身）→ 放两套地图库、省界数据、授权文本
# → NSIS 安装程序（installer\TravelPlanner.nsi）+ 便携版 zip（同一份文件），量大小。
# 由 打包.bat 调用。PowerShell 5.1 需要本文件带 UTF-8 BOM。
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
$Zip = Join-Path $Root "dist\$Name-portable-win64.zip"
$Setup = Join-Path $Root "dist\$Name-Setup.exe"
$Nsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'
$Result = Join-Path $Root '打包-结果.txt'
Set-Content -Path $Result -Value "打包 $Ver  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
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
# assets/app/logo 是图标的母版和各尺寸导出（1.3.2），程序只用 assets/app/icon.ico、icon-256.png，不打进安装包
Remove-Item (Join-Path $Stage 'assets/app/logo') -Recurse -Force -ErrorAction SilentlyContinue
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
# 0.14.2 瘦身：qmltooling（QML 调试）、generic（触摸输入）、iconengines（SVG 图标）、networkinformation 不带；
# 图片格式只留 jpeg、gif、ico 和 qwebp（WebP 照片，libwebp BSD-3），没用 SVG；定位只留 qtposition_winrt（NMEA 要 Qt6SerialPort，positionpoll 是区域监控）；
# TLS 只用 Windows 自带的 schannel（OpenSSL 后端没带 OpenSSL 本身，用不上）
$common = @('--release', '--no-translations', '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--no-opengl-sw', '--no-compiler-runtime',
            '--skip-plugin-types', 'qmltooling,generic,iconengines,networkinformation',
            '--exclude-plugins', 'qtiff,qicns,qtga,qwbmp,qpdf,qsvg,qtposition_nmea,qtposition_positionpoll,qopensslbackend,qcertonlybackend')
# windeployqt 的警告走 stderr，PowerShell 5.1 在 Stop 下会当成出错：这一段改成 Continue，只看退出码
$ErrorActionPreference = 'Continue'
& $wdq @common --qmldir (Join-Path $SrcLink 'qml') (Join-Path $Stage 'TravelPlanner.exe') *> (Join-Path $Dev 'windeployqt-release.log')
$rcW = $LASTEXITCODE
Get-ChildItem (Join-Path $Stage 'maplibre') -Recurse -Filter *.dll | ForEach-Object {
    & $wdq @common --no-quick-import --dir $Stage $_.FullName *>> (Join-Path $Dev 'windeployqt-release.log')
}
$ErrorActionPreference = 'Stop'
if ($rcW -ne 0) { Fail 'windeployqt 出错（日志 D:\TravelPlannerNative-dev\windeployqt-release.log）' }
# 0.14.2 瘦身：Qt Quick Controls 只留 Basic（程序不用 Controls，只有选文件框的非原生备用界面会用，main.cpp 固定 Basic；
# Controls 的 qmldir 里其他样式是 optional import，缺了不报错）；再去掉 windeployqt 留下的空 QML 模块文件夹
foreach ($s in 'FluentWinUI3', 'Fusion', 'Imagine', 'Material', 'Universal', 'Windows') {
    Remove-Item "$Stage\Qt6QuickControls2$s.dll", "$Stage\Qt6QuickControls2${s}StyleImpl.dll", "$Stage\qml\QtQuick\Controls\$s", "$Stage\qml\QtQuick\Dialogs\quickimpl\qml\+$s" -Recurse -Force -ErrorAction SilentlyContinue
}
Remove-Item "$Stage\Qt6QuickNativeStyle.dll", "$Stage\qml\QtQuick\Controls\Native", "$Stage\qml\QtQuick\NativeStyle" -Recurse -Force -ErrorAction SilentlyContinue
do {
    $empty = @(Get-ChildItem "$Stage\qml" -Recurse -Directory | Where-Object { -not (Get-ChildItem $_.FullName -Force) })
    $empty | Remove-Item -Force
} while ($empty.Count -gt 0)
# VC++ 运行库：直接放 DLL（程序目录本地部署，不带 vc_redist 安装包）
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if ($crt) { Copy-Item (Join-Path $crt.FullName '*.dll') $Stage; Say "  VC++ 运行库：$($crt.Name)" } else { Say '  [!] 没找到 VC++ 运行库 DLL' 'Yellow' }
# 授权文本（0.14.2 按安装目录实际文件核对，清单见 LICENSES\README.txt 和 设置 → 关于）
$lic = Join-Path $Stage 'LICENSES'
New-Item -ItemType Directory -Force -Path (Join-Path $lic 'maplibre-native-qt') | Out-Null
Copy-Item (Join-Path $Root 'third_party\maplibre-native-qt\LICENSES\*') (Join-Path $lic 'maplibre-native-qt')
Copy-Item (Join-Path $Root 'assets\fonts\LICENSE-*.txt'), (Join-Path $Root 'assets\licenses\*.txt') $lic
$qtLic = Join-Path (Split-Path (Split-Path $Qt)) 'Licenses'
if (-not (Test-Path "$qtLic\LICENSE")) { Fail "没找到 Qt 授权文本 $qtLic\LICENSE" }
New-Item -ItemType Directory -Force -Path (Join-Path $lic 'Qt') | Out-Null
Copy-Item "$qtLic\LICENSE" (Join-Path $lic 'Qt')   # GPL-3.0 + LGPL-3.0 全文（LICENSE.FDL 是 Qt 文档的授权，不带）
# Qt 内置的第三方组件（libpng、libjpeg、FreeType、HarfBuzz、zlib、PCRE2、libwebp……）：从 Qt 自带文档的 attribution 页取全文。
# 只排除别的平台（Android / macOS / Linux）和示例里的；libtiff、Material 样式的阴影只在真的带了对应插件时列。
$docs = Join-Path (Split-Path (Split-Path $Qt)) ('Docs\Qt-' + (Split-Path (Split-Path $Qt) -Leaf))
$skip = 'android|cocoa|xcb|dbus|qeventdispatcher-cf|forkfd|kwin|extra-cmake-modules|dejayvu|gear-icon|georama|map-of-europe|weather-icons|work-sans|material-symbols-and-icons|titilliumweb'
if (-not (Test-Path (Join-Path $Stage 'imageformats\qtiff.dll'))) { $skip += '|libtiff' }
if (-not (Test-Path (Join-Path $Stage 'qml\QtQuick\Controls\Material'))) { $skip += '|shadow-angular-material' }
$attr = foreach ($m in 'qtcore', 'qtgui', 'qtnetwork', 'qtqml', 'qtquick', 'qtquickcontrols', 'qtimageformats', 'qtpositioning') {
    Get-ChildItem (Join-Path $docs $m) -Filter "$m-attribution-*.html" -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch $skip }
}
if (-not $attr) { Fail "没找到 Qt 文档里的第三方组件授权（$docs）" }
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine("Qt $(Split-Path (Split-Path $Qt) -Leaf) 内置的第三方组件和授权全文 / Third-party components bundled in Qt and their licenses")
[void]$sb.AppendLine('（从 Qt 自带文档的 attribution 页面整理；Qt 本身的 LGPL-3.0 见同目录 LICENSE）')
foreach ($f in $attr) {
    $h = [IO.File]::ReadAllText($f.FullName)
    $h = [regex]::Replace($h, '(?s)<(script|style|head)[^>]*>.*?</\1>', '')
    $h = [regex]::Replace($h, '(?i)<br\s*/?>|</(p|div|h\d|li|tr)>', "`n")
    $h = [Net.WebUtility]::HtmlDecode([regex]::Replace($h, '<[^>]+>', ''))
    $i = $h.LastIndexOf('Reference Documentation'); if ($i -ge 0) { $h = $h.Substring($i + 23) }
    $j = $h.IndexOf([char]0xA9 + ' 20'); if ($j -gt 0) { $h = $h.Substring(0, $j) }
    $t = (($h -split "`r?`n" | ForEach-Object { $_.TrimEnd() }) -join "`r`n") -replace "(`r`n\s*){3,}", "`r`n`r`n"
    [void]$sb.AppendLine("`r`n==============================================================================").AppendLine($t.Trim())
}
[IO.File]::WriteAllText((Join-Path $lic 'Qt\Qt-third-party.txt'), $sb.ToString(), (New-Object Text.UTF8Encoding $true))
Say "  Qt 第三方组件授权：$($attr.Count) 项"
Get-ChildItem $Stage -Recurse -Include *.pdb, *.ilk | Remove-Item -Force

# ---- 3. 检查不该带的 ----
Say '[3/5] 检查…'
foreach ($bad in 'Qt6ShaderTools.dll', 'Qt6QmlCompiler.dll', 'Qt6Quick3D.dll', 'Qt6WebEngineCore.dll') {
    if (Test-Path (Join-Path $Stage $bad)) { Say "  [!] 带上了不该带的 $bad" 'Yellow' }
}
if (Test-Path (Join-Path $Stage 'Qt6QuickEffects.dll')) { Say '  [i] 带了 Qt6QuickEffects.dll（LGPL-3，已核实可用）' }
if (Get-ChildItem $Stage -Recurse -Filter 'qmldbg_*.dll') { Fail '带上了 QML 调试插件 qmldbg_*.dll' }
foreach ($need in 'position\qtposition_winrt.dll', 'imageformats\qwebp.dll', 'imageformats\qjpeg.dll', 'platforms\qwindows.dll', 'tls\qschannelbackend.dll', 'LICENSES\Qt\LICENSE', 'LICENSES\Qt\Qt-third-party.txt', 'LICENSES\README.txt') {
    if (-not (Test-Path (Join-Path $Stage $need))) { Fail "缺少 $need" }
}

# ---- 4. 安装程序 + 便携版 zip ----
Say '[4/5] NSIS 安装程序和便携版 zip…'
foreach ($old in $Zip, $Setup) { if (Test-Path $old) { Move-Item $old (Join-Path $Root ('【删除】\' + (Split-Path $old -Leaf) + '.' + (Get-Date -Format 'HHmmss'))) } }
# 卸载 / 覆盖安装要删的程序文件：按收集目录的顶层文件和文件夹生成（data\、settings.json、cache\ 不在里面）
$nsh = Join-Path $Dev 'files.nsh'
$lines = @('!macro RemoveProgramFiles')
Get-ChildItem $Stage -Force | Sort-Object Name | ForEach-Object {
    if ($_.PSIsContainer) { $lines += "  RMDir /r `"`$INSTDIR\$($_.Name)`"" } else { $lines += "  Delete `"`$INSTDIR\$($_.Name)`"" }
}
$lines += '!macroend'
[IO.File]::WriteAllLines($nsh, $lines, (New-Object Text.UTF8Encoding $true))
if (-not (Test-Path $Nsis)) { Fail "没找到 NSIS：$Nsis" }
$VerNum = ([regex]::Match($Ver, '^\d+\.\d+\.\d+')).Value
& $Nsis /V2 /INPUTCHARSET UTF8 "/DVERSION=$Ver" "/DVERNUM=$VerNum" "/DSTAGE=$Stage" "/DOUTFILE=$Setup" "/DFILES=$nsh" (Join-Path $Root 'installer\TravelPlanner.nsi') *> (Join-Path $Dev 'nsis.log')
if ($LASTEXITCODE -ne 0) { Fail 'NSIS 出错（日志 D:\TravelPlannerNative-dev\nsis.log）' }
# 便携版：同一份文件，解压即用（数据在解压目录的 data\）
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($Stage, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)

# ---- 5. 大小对比 ----
Say '[5/5] 大小对比：'
$dirSize = DirSize $Stage
$zipSize = (Get-Item $Zip).Length
$fileCount = (Get-ChildItem $Stage -Recurse -File).Count
Say "  原生 $Ver：安装后约 $(MB $dirSize)（$fileCount 个文件），安装包 $(MB (Get-Item $Setup).Length)，便携版 zip $(MB $zipSize)"
$eSetup = Get-ChildItem 'D:\旅行计划App\dist' -Filter 'TravelPlanner-Setup-6.3.0.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($eSetup) { Say "  Electron v6.3.0：安装包 $(MB $eSetup.Length)" }
$eUnp = 'D:\旅行计划App\dist\win-unpacked'
if (Test-Path $eUnp) { Say "  Electron（dist\win-unpacked，最近一次打包）：安装后约 $(MB (DirSize $eUnp))" }
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
Say "完成：$Setup" 'Green'
Say "      $Zip" 'Green'
exit 0
