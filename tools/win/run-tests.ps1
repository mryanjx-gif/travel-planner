# 编译并运行单元测试（tests\tst_data：JSON 读写、Electron data.js 格式、保存流程（0.6.0 起是 travel.json）、锁文件、
# 从 Electron 版导入（含真实数据"每个值都找得到"）、真实数据往返；
# tests\tst_app（0.4.0）：设置文件、数据文件夹、窗口位置、翻译词典和缺英文检查、日志；
# tests\tst_photo（0.7.0）：EXIF、缩略图、旋转、导入、清理，用 tests\photos.txt 里的测试照片；
# tests\tst_map（0.8.0）：投影和相机、按范围缩放、最小缩放、飞行、昼夜阴影、"已解锁"上色、路线几何；
# tests\tst_net（0.8.4）：统一请求通道（本机假服务器）、批量算道路路线）。由 单元测试.bat 调用。
# 真实数据往返测试只读 -DataDir（默认 D:\旅行计划数据，Electron 版的数据）里的 data.js、data.backup.js、*.js 和 .history\*.js，不写入。
# 保存流程的测试都在临时文件夹里做（%TEMP% 下，测完自动删除）。
# PowerShell 5.1 需要本文件带 UTF-8 BOM。
# 0.7.2：-Build 指定编译目录（验证.bat 用程序的编译目录，测试和程序一起编）；-NoBuild 只运行不编译；
#        结果文件里 UTF-8 和 GBK 混在一起（数据行名字是 UTF-8、Qt 的消息是本地代码页），逐行判断编码，中文不再乱码。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Qt = 'D:\Qt\6.12.0\msvc2022_64',
    [string]$DataDir = 'D:\旅行计划数据',
    [string]$Build = '',
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
$Dev = (Split-Path $Root -Qualifier) + '\TravelPlannerNative-dev'
if (-not $Build) { $Build = Join-Path $Dev 'tests' }
$Log = Join-Path $Root '单元测试-结果.txt'
New-Item -ItemType Directory -Force -Path $Dev | Out-Null
Set-Content -Path $Log -Value "单元测试  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Log -Value $m -Encoding UTF8 }
function Fail([string]$m) { Say "[×] $m" 'Red'; exit 1 }
# 差异清单 #36：cl 的 /showIncludes 前缀按控制台代码页编码（这台电脑只有中文资源，VSLANG=1033 不管用）。
# 配置和编译都固定在系统 ANSI 代码页，前缀才和配置时记下的一致，ninja 才记得住头文件依赖；.bat 的 chcp 65001 不影响。
function Use-AnsiConsole {
    $acp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP
    try { [Console]::OutputEncoding = [Text.Encoding]::GetEncoding([int]$acp) } catch { Write-Host "  [!] 没能设控制台代码页：$($_.Exception.Message)" -ForegroundColor Yellow }
}
function Run([string]$title, [string]$exe, [string[]]$argv) {
    Add-Content -Path $Log -Value "> $exe $($argv -join ' ')" -Encoding UTF8
    $ErrorActionPreference = 'Continue'
    $cp0 = [Console]::OutputEncoding
    Use-AnsiConsole
    & $exe @argv 2>&1 | ForEach-Object { $l = "$_"; if ($l -eq 'System.Management.Automation.RemoteException') { return }; Add-Content -Path $Log -Value $l -Encoding UTF8; if ($l -match 'error|错误|FAILED') { Write-Host "  $l" -ForegroundColor Red } }
    $rc = $LASTEXITCODE
    try { [Console]::OutputEncoding = $cp0 } catch {}
    $ErrorActionPreference = 'Stop'
    if ($rc -ne 0) {
        Write-Host '---- 最后 30 行 ----' -ForegroundColor Yellow
        Get-Content $Log -Tail 30 | ForEach-Object { Write-Host $_ }
        Fail "$title 失败（退出码 $rc）"
    }
}

# 逐行判断编码：能按 UTF-8 解就按 UTF-8，否则按 GBK（中文 Windows 的本地代码页）
function Read-Mixed([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    $gbk = [System.Text.Encoding]::GetEncoding(936)
    $out = New-Object System.Collections.Generic.List[string]
    $start = 0
    for ($i = 0; $i -le $bytes.Length; $i++) {
        if ($i -eq $bytes.Length -or $bytes[$i] -eq 10) {
            $len = $i - $start
            if ($len -gt 0 -and $bytes[$i - 1] -eq 13) { $len-- }
            if ($len -gt 0 -or $i -lt $bytes.Length) {
                $seg = New-Object byte[] ([Math]::Max($len, 0))
                if ($len -gt 0) { [Array]::Copy($bytes, $start, $seg, 0, $len) }
                try { $out.Add($utf8.GetString($seg)) } catch { $out.Add($gbk.GetString($seg)) }
            }
            $start = $i + 1
        }
    }
    return $out
}

if ($NoBuild) {
    Say '[1-3/4] 已经编译好（验证.bat），跳过编译'
} else {
Say '[1/4] 准备 VS 2022 x64 编译环境…'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail '没找到 VS 2022 Build Tools，先运行 检查环境.bat' }
foreach ($line in (cmd /c "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set")) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
if (-not (Test-Path (Join-Path $Qt 'lib\cmake\Qt6\Qt6Config.cmake'))) { Fail "没找到 Qt：$Qt" }

# 源码经英文路径的目录联接访问（Qt 的部分工具读不了中文路径；和 编译.bat 用同一个联接）
$SrcLink = Join-Path $Dev 'src'
if (-not (Test-Path (Join-Path $SrcLink 'CMakeLists.txt'))) {
    if (Test-Path $SrcLink) { cmd /c rmdir "`"$SrcLink`"" | Out-Null }
    cmd /c mklink /J "`"$SrcLink`"" "`"$Root`"" | Out-Null
}
if (-not (Test-Path (Join-Path $SrcLink 'CMakeLists.txt'))) { Fail "建立目录联接失败：$SrcLink → $Root" }

Say '[2/4] 生成编译配置…'
if (Test-Path (Join-Path $Build 'build.ninja')) {
    Say '  已经配置过，跳过（CMakeLists 改了时编译会自动重新配置）'
} else {
    Run '生成编译配置' 'cmake' @('-S', $SrcLink, '-B', $Build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_PREFIX_PATH=$Qt", '-DTP_BUILD_TESTS=ON')
}

Say '[3/4] 编译测试程序（只编 tst_data、tst_app、tst_photo、tst_map、tst_net、tst_ai、tst_export，不编整个程序）…'
Run '编译' 'cmake' @('--build', $Build, '--target', 'tst_data', 'tst_app', 'tst_photo', 'tst_map', 'tst_net', 'tst_ai', 'tst_export')
}

Say '[4/4] 运行测试…'
$env:PATH = (Join-Path $Qt 'bin') + ';' + $env:PATH
if (Test-Path (Join-Path $DataDir 'data.js')) {
    $env:TP_ROUNDTRIP_DIRS = $DataDir
    Say "  真实数据往返：$DataDir（只读）"
} else {
    Remove-Item Env:TP_ROUNDTRIP_DIRS -ErrorAction SilentlyContinue
    Say "  [!] 没找到 $DataDir\data.js，真实数据往返只测 testdata\data.js" 'Yellow'
}
# 结果文件名只用英文。qt_standard_project_setup 在 Windows 上把所有 exe 放到编译目录顶层（0.3.2 起两个位置都找）。
# 0.4.0：Qt Test 在 Windows 上按系统的本地代码页（中文系统是 GBK）写结果文件，按 Default 编码读，中文不再乱码。
$rc = 0
foreach ($name in 'tst_data', 'tst_app', 'tst_photo', 'tst_map', 'tst_net', 'tst_ai', 'tst_export') {
    $out = Join-Path $Build "$name-output.txt"
    if (Test-Path $out) { Remove-Item $out -Force }
    $exe = @((Join-Path $Build "$name.exe"), (Join-Path $Build "tests\$name.exe")) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exe) { Fail "编译成功了，但找不到 $name.exe（在 $Build 和 $Build\tests 里都没有）" }
    Say "  测试程序：$exe"
    $ErrorActionPreference = 'Continue'
    $global:LASTEXITCODE = -999
    & $exe -o "$out,txt" -o '-,txt' 2>&1 | ForEach-Object { $l = "$_"; if ($l -match '^(FAIL|Totals)') { Write-Host $l } }
    $one = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($one -eq -999) { Fail "测试程序没有运行起来：$exe" }
    if (-not (Test-Path $out)) { Fail "$name 没有写出结果文件（退出码 $one）" }
    Read-Mixed $out | ForEach-Object {
        Add-Content -Path $Log -Value $_ -Encoding UTF8
        if ($_ -match '^FAIL') { Write-Host $_ -ForegroundColor Red }
        elseif ($_ -match '^(QINFO|QWARN|Totals)') { Write-Host $_ }
    }
    $rc += $one
}
Say ''
if ($rc -eq 0) { Say '全部通过。' 'Green' } else { Say "有 $rc 项没通过（红色的行）。" 'Red' }
exit $rc
