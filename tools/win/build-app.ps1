# 编译程序本身（RelWithDebInfo：优化 + 调试信息），并放入两套地图库、收集 Qt 运行库。不运行程序。
# 由 编译.bat 调用。PowerShell 5.1 需要本文件带 UTF-8 BOM。
# 为什么不直接在 bat 里写：cmd 在 UTF-8 代码页下读长的中文行会错位（0.2.0 实测第一行注释被当成命令）；
# 为什么要目录联接：Qt 的 qmlimportscanner 读不了中文路径（0.2.0 实测 "No such file or directory: D:/旅行计划Native"）。
# 0.5.1：编译前先检查开发版程序是否还开着（开着时链接会报 LNK1168）；windeployqt 的完整输出放到程序目录的
#        windeployqt-日志.txt，编译-结果.txt 里只留出错的行和一行总结。
# 0.7.2：省时间——已经配置过就不再重新配置（CMakeLists 改了时 ninja 会自动重新配置）；两套地图库没变就不再复制、
#        不再对它们跑 windeployqt；-WithTests 时单元测试也编进同一个目录（验证.bat 用，共用的代码只编一次）；
#        -NoExplorer 编完不打开文件夹；-Reconfigure 强制重新配置。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Qt = 'D:\Qt\6.12.0\msvc2022_64',
    [string]$BuildType = 'RelWithDebInfo',
    [string]$Build = '',
    [switch]$WithTests,
    [switch]$NoExplorer,
    [switch]$Reconfigure
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
# 开发用临时目录放在源码所在盘的英文路径（PO 2026-11-02：放 D 盘，不放 C 盘）
$Dev = (Split-Path $Root -Qualifier) + '\TravelPlannerNative-dev'
if (-not $Build) { $Build = Join-Path $Dev 'app' }
$Log = Join-Path $Root '编译-结果.txt'
New-Item -ItemType Directory -Force -Path $Dev | Out-Null
Set-Content -Path $Log -Value "编译 $BuildType  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Log -Value $m -Encoding UTF8 }
function Fail([string]$m) { Say "[×] $m" 'Red'; Say '请把 编译-结果.txt 发给 Claude（或告诉我，我直接读）。' 'Yellow'; exit 1 }
function Run([string]$title, [string]$exe, [string[]]$argv) {
    Add-Content -Path $Log -Value "> $exe $($argv -join ' ')" -Encoding UTF8
    $ErrorActionPreference = 'Continue'
    & $exe @argv 2>&1 | ForEach-Object { $l = "$_"; if ($l -eq 'System.Management.Automation.RemoteException') { return }; Add-Content -Path $Log -Value $l -Encoding UTF8; if ($l -match 'error|错误|FAILED') { Write-Host "  $l" -ForegroundColor Red } }
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($rc -ne 0) {
        Write-Host '---- 最后 30 行 ----' -ForegroundColor Yellow
        Get-Content $Log -Tail 30 | ForEach-Object { Write-Host $_ }
        Fail "$title 失败（退出码 $rc）"
    }
}

# 0.5.1：开发版程序还开着（包括收在托盘里）时，链接会因为 TravelPlanner.exe 被占用而失败（LNK1168），先提示退出
$exeTarget = Join-Path $Build 'TravelPlanner.exe'
$running = @(Get-Process -Name 'TravelPlanner' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and ($_.Path -ieq $exeTarget) })
if ($running.Count -gt 0) {
    Say '[×] 旅行计划（开发版）还开着（可能收在右下角托盘里），编译时没法替换 TravelPlanner.exe。' 'Red'
    Say '    先退出它（托盘图标右键 → 退出，或在窗口里按 Ctrl+Q），再运行 编译.bat。' 'Yellow'
    exit 2
}

Say '[1/5] 准备 VS 2022 x64 编译环境…'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail '没找到 VS 2022 Build Tools，先运行 检查环境.bat' }
foreach ($line in (cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set")) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
if (-not (Test-Path (Join-Path $Qt 'lib\cmake\Qt6\Qt6Config.cmake'))) { Fail "没找到 Qt：$Qt" }
$mln = @{}
foreach ($b in 'vulkan', 'opengl') {
    $d = Join-Path $Root "third_party\maplibre-native-qt\install-$b"
    if (Test-Path (Join-Path $d 'qml\MapLibre\qmldir')) { $mln[$b] = $d } else { Say "  [!] 没有 $b 版 maplibre" 'Yellow' }
}
if ($mln.Count -eq 0) { Fail '还没有编译好的 maplibre：先运行 编译maplibre.bat' }

# 0.2.1 以前临时目录在 C 盘：把那里的两个目录联接拆掉（rmdir 只删联接本身，不碰 D 盘源码），C 盘旧目录之后可以整个删掉
$oldDev = Join-Path $env:LOCALAPPDATA 'TravelPlannerNative-dev'
foreach ($j in 'src', 'mln-src') {
    $p = Join-Path $oldDev $j
    if ((Test-Path $p) -and ((Get-Item $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { cmd /c rmdir "`"$p`"" | Out-Null }
}
if (Test-Path $oldDev) { Say "  [i] C 盘旧的临时目录 $oldDev 已经不用了，可以整个删掉（里面没有要保留的东西）" }

# 源码经英文路径的目录联接访问
$SrcLink = Join-Path $Dev 'src'
if (Test-Path $SrcLink) { cmd /c rmdir "`"$SrcLink`"" | Out-Null }
cmd /c mklink /J "`"$SrcLink`"" "`"$Root`"" | Out-Null
if (-not (Test-Path (Join-Path $SrcLink 'CMakeLists.txt'))) { Fail "建立目录联接失败：$SrcLink → $Root" }
Say "  源码目录联接：$SrcLink → $Root"

Say '[2/5] 生成编译配置…'
$cache = Join-Path $Build 'CMakeCache.txt'
$hasTests = (Test-Path $cache) -and (Select-String -Path $cache -Pattern '^TP_BUILD_TESTS:BOOL=ON' -Quiet)
$sameType = (Test-Path $cache) -and (Select-String -Path $cache -Pattern "^CMAKE_BUILD_TYPE:STRING=$BuildType`$" -Quiet)
if ((Test-Path (Join-Path $Build 'build.ninja')) -and $sameType -and -not $Reconfigure -and (-not $WithTests -or $hasTests)) {
    Say '  已经配置过，跳过（CMakeLists 改了时编译会自动重新配置）'
} else {
    $cfg = @('-S', $SrcLink, '-B', $Build, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$BuildType", "-DCMAKE_PREFIX_PATH=$Qt")
    if ($WithTests) { $cfg += '-DTP_BUILD_TESTS=ON' }
    Run '生成编译配置' 'cmake' $cfg
}

Say '[3/5] 编译…'
if ($WithTests) { Run '编译' 'cmake' @('--build', $Build) }
else { Run '编译' 'cmake' @('--build', $Build, '--target', 'TravelPlanner') }

Say '[4/5] 放入两套地图库（maplibre\vulkan、maplibre\opengl）…'
# 0.7.2：地图库没变（按两套 install 目录里最新的文件时间）就不再复制
$stampFile = Join-Path $Build 'maplibre\.stamp'
$stamp = ($mln.Keys | Sort-Object | ForEach-Object { $k = $_; $t = (Get-ChildItem $mln[$k] -Recurse -File | Measure-Object -Property LastWriteTimeUtc -Maximum).Maximum; "$k=$($t.Ticks)" }) -join ';'
$mlnFresh = $Reconfigure -or -not ((Test-Path $stampFile) -and ((Get-Content $stampFile -Raw).Trim() -eq $stamp))
if (-not $mlnFresh) { Say '  地图库没变，跳过' }
foreach ($b in $(if ($mlnFresh) { $mln.Keys } else { @() })) {
    foreach ($sub in 'bin', 'qml') {
        $dst = Join-Path $Build "maplibre\$b\$sub"
        if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        if ($sub -eq 'bin') { Copy-Item (Join-Path $mln[$b] 'bin\*.dll') $dst } else { Copy-Item (Join-Path $mln[$b] 'qml\*') $dst -Recurse }
    }
    Say "  $b：已放入"
}

Say '[5/5] 复制运行需要的 Qt 文件（windeployqt）…'
$wdq = Join-Path $Qt 'bin\windeployqt.exe'
$common = @('--no-translations', '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--no-opengl-sw', '--no-compiler-runtime')
# 0.5.1：windeployqt 每个文件一行（"is up to date" 占大多数，约 1900 行），完整输出放到程序目录的 windeployqt-日志.txt，
# 编译-结果.txt 只记出错 / 警告的行和一行总结
$wdqLog = Join-Path $Build 'windeployqt-日志.txt'
Set-Content -Path $wdqLog -Value "windeployqt  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function RunWdq([string[]]$argv) {
    Add-Content -Path $wdqLog -Value "> $wdq $($argv -join ' ')" -Encoding UTF8
    $ErrorActionPreference = 'Continue'
    $lines = @(& $wdq @argv 2>&1 | ForEach-Object { "$_" })
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($lines.Count -gt 0) { Add-Content -Path $wdqLog -Value $lines -Encoding UTF8 }
    $script:wdqUpToDate += @($lines | Where-Object { $_ -match 'is up to date' }).Count
    $script:wdqCopied += @($lines | Where-Object { $_ -match '^(Updating|Copying|Creating) ' }).Count
    return @{ rc = $rc; lines = $lines }
}
$script:wdqUpToDate = 0
$script:wdqCopied = 0
$r = RunWdq ($common + @('--qmldir', (Join-Path $SrcLink 'qml'), (Join-Path $Build 'TravelPlanner.exe')))
$bad = @($r.lines | Where-Object { $_ -match 'error|warning|cannot|unable|failed|错误' })
foreach ($l in $bad) { Say "  $l" 'Yellow' }
if ($r.rc -ne 0) {
    Write-Host '---- windeployqt 最后 30 行 ----' -ForegroundColor Yellow
    $r.lines | Select-Object -Last 30 | ForEach-Object { Write-Host $_; Add-Content -Path $Log -Value $_ -Encoding UTF8 }
    Fail "windeployqt 失败（退出码 $($r.rc)），完整输出见 $wdqLog"
}
# 地图库自己依赖的 Qt 库（Qt6OpenGL、Qt6Sql 等）也放到程序目录；找不到 QMapLibre 之类非 Qt 库的提示可以忽略（只记在 windeployqt-日志.txt）
# 0.7.2：地图库没变时跳过（这一步每个 dll 跑一次 windeployqt，最慢）
if ($mlnFresh) {
    Get-ChildItem (Join-Path $Build 'maplibre') -Recurse -Filter *.dll | ForEach-Object {
        RunWdq ($common + @('--no-quick-import', '--dir', $Build, $_.FullName)) | Out-Null
    }
    Set-Content -Path $stampFile -Value $stamp -Encoding ASCII
}
Say "  windeployqt 完成：复制 / 更新 $($script:wdqCopied) 个，已是最新 $($script:wdqUpToDate) 个（完整输出：$wdqLog）"

Say ''
Say "完成：$(Join-Path $Build 'TravelPlanner.exe')" 'Green'
Say "双击它就能运行。数据文件夹按设置里记住的打开（第一次运行会问用哪个）；设置在程序目录的 settings.json，日志在 %LOCALAPPDATA%\TravelPlannerNative\logs（F12 打开）。"
Say "指定后端：TravelPlanner.exe --backend=opengl 或 --backend=vulkan；临时打开别的数据：--data <数据文件夹>"
if (-not $NoExplorer) { Start-Process explorer.exe $Build }
exit 0
