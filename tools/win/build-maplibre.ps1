# 编译 maplibre-native-qt：Vulkan 版和 OpenGL 版各一套（一次编译只能带一个后端，见 claude/v7.0.0-b记录.md）
# 由 编译maplibre.bat 调用。PowerShell 5.1 需要本文件带 UTF-8 BOM。
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Qt = 'D:\Qt\6.12.0\msvc2022_64',
    [string]$Only = ''          # 只编一套：vulkan 或 opengl
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path.TrimEnd('\')
$Mln = Join-Path $Root 'third_party\maplibre-native-qt'
$Src = Join-Path $Mln 'src'
# 开发用临时目录放在源码所在盘的英文路径（PO 2026-11-02：放 D 盘，不放 C 盘）
$Dev = (Split-Path $Root -Qualifier) + '\TravelPlannerNative-dev'
$LogDir = Join-Path $Root '编译maplibre-日志'
$Result = Join-Path $Root '编译maplibre-结果.txt'
New-Item -ItemType Directory -Force -Path $Dev, $LogDir | Out-Null
$T0 = Get-Date
Set-Content -Path $Result -Value "编译 maplibre-native-qt  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -Encoding UTF8
function Say([string]$m, [string]$color = 'Gray') { Write-Host $m -ForegroundColor $color; Add-Content -Path $Result -Value $m -Encoding UTF8 }
function Fail([string]$m) { Say "[×] $m" 'Red'; Say "请把 编译maplibre-结果.txt 和 编译maplibre-日志\ 里最新的 .log 发给 Claude。" 'Yellow'; exit 1 }
function Fmt([double]$sec) { $s = [int][math]::Max(0, $sec); if ($s -ge 3600) { '{0} 小时 {1:00} 分' -f [math]::Floor($s / 3600), [math]::Floor(($s % 3600) / 60) } else { '{0} 分 {1:00} 秒' -f [math]::Floor($s / 60), ($s % 60) } }

# ---- 检查 ----
if (-not (Test-Path (Join-Path $Src '.解压完成.txt'))) { Fail '还没有解压源码：先运行 准备maplibre源码.bat' }
if (-not (Test-Path (Join-Path $Qt 'lib\cmake\Qt6\Qt6Config.cmake'))) { Fail "没找到 Qt：$Qt" }

# ---- VS 2022 x64 编译环境（把 vcvars64.bat 设的环境变量搬进来）----
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail '没找到 VS 2022 Build Tools' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($line in (cmd /c "`"$vcvars`" >nul 2>&1 && set")) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Fail 'vcvars64.bat 之后找不到 cl.exe' }
if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue)) { Fail '找不到 ninja.exe' }

# ---- 源码走英文路径（目录联接），避开中文路径 ----
$SrcLink = Join-Path $Dev 'mln-src'
if (Test-Path $SrcLink) { cmd /c rmdir "`"$SrcLink`"" | Out-Null }
cmd /c mklink /J "`"$SrcLink`"" "`"$Src`"" | Out-Null
if (-not (Test-Path (Join-Path $SrcLink 'CMakeLists.txt'))) { Fail "建立目录联接失败：$SrcLink → $Src" }

# ---- 线程数和预计时间 ----
$cores = [Environment]::ProcessorCount
$ramGB = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$jobs = [int][math]::Max(1, [math]::Min($cores, [math]::Floor($ramGB / 1.5)))
# 你电脑实测（MSVC，8 线程，2026-11-02）：Vulkan 版 18 分 44 秒 ≈ 150 核·分钟，OpenGL 版 17 分 20 秒 ≈ 139 核·分钟
# （云端 GCC 只要 26 / 19 核·分钟，MSVC 慢 6–7 倍）。按核·分钟除以并行数估算。
# 注意：PowerShell 变量名不分大小写，显示用的名字不能叫 $B（会覆盖 $b）
$est = @{ opengl = 139 / $jobs; vulkan = 150 / $jobs }
$sets = @('vulkan', 'opengl')
if ($Only) { $sets = @($Only.ToLower()) }
$estTotal = ($sets | ForEach-Object { $est[$_] } | Measure-Object -Sum).Sum
Say "[i] CPU $cores 线程，内存 $ramGB GB → 同时编译 $jobs 个文件"
Say ("[i] 预计：" + (($sets | ForEach-Object { "$_ 约 $([math]::Ceiling($est[$_])) 分钟" }) -join '，') + "，合计约 $([math]::Ceiling($estTotal)) 分钟（估算，可能差一倍）")
Say "[i] 临时文件在 $Dev；成品放 third_party\maplibre-native-qt\install-vulkan\、install-opengl\"
Say ''

$n = 0
foreach ($b in $sets) {
    $n++
    $Disp = if ($b -eq 'vulkan') { 'Vulkan' } else { 'OpenGL' }
    $flag = if ($b -eq 'vulkan') { 'MLN_WITH_VULKAN' } else { 'MLN_WITH_OPENGL' }
    $bd = Join-Path $Dev ("m-" + $b.Substring(0, 2))
    $inst = Join-Path $Mln "install-$b"
    $log = Join-Path $LogDir "$b.log"
    Set-Content -Path $log -Value "== $Disp $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==" -Encoding UTF8
    Say "==== 第 $n 套 / 共 $($sets.Count) 套：$Disp ====" 'Cyan'
    $tb = Get-Date

    Say '  生成编译配置…'
    $ErrorActionPreference = 'Continue'
    & cmake -S $SrcLink -B $bd -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$Qt" "-D$flag=ON" `
        -DMLN_QT_WITH_LOCATION=OFF -DMLN_QT_WITH_WIDGETS=OFF -DMLN_WITH_WERROR=OFF `
        -DCMAKE_POLICY_VERSION_MINIMUM=3.19 "-DCMAKE_INSTALL_PREFIX=$inst" 2>&1 |
        ForEach-Object { Add-Content -Path $log -Value "$_" -Encoding UTF8 }
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($rc -ne 0) { Get-Content $log -Tail 30 | ForEach-Object { Write-Host $_ }; Fail "$Disp 生成编译配置失败（日志 编译maplibre-日志\$b.log）" }

    Say "  编译（$jobs 个并行）…"
    $env:NINJA_STATUS = '[%f/%t] '
    $last = Get-Date; $done = 0; $total = 0
    $ErrorActionPreference = 'Continue'
    & cmake --build $bd -j $jobs 2>&1 | ForEach-Object {
        $line = "$_"
        Add-Content -Path $log -Value $line -Encoding UTF8
        if ($line -match '^\[(\d+)/(\d+)\]') {
            $done = [int]$Matches[1]; $total = [int]$Matches[2]
            $el = ((Get-Date) - $tb).TotalSeconds
            $pct = [int](100 * $done / [math]::Max(1, $total))
            $eta = if ($done -ge 20) { Fmt ($el / $done * ($total - $done)) } else { '计算中' }
            $status = "第 $n/$($sets.Count) 套 $Disp  [$done/$total $pct%]  已用 $(Fmt $el)  预计还要 $eta"
            Write-Progress -Activity '编译 maplibre-native-qt' -Status $status -PercentComplete $pct
            if (((Get-Date) - $last).TotalSeconds -ge 30) { Write-Host "  $status"; $last = Get-Date }
        } elseif ($line -match 'error|错误') { Write-Host "  $line" -ForegroundColor Red }
    }
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Write-Progress -Activity '编译 maplibre-native-qt' -Completed
    if ($rc -ne 0) {
        Write-Host '---- 日志最后 40 行 ----' -ForegroundColor Yellow
        Get-Content $log -Tail 40 | ForEach-Object { Write-Host $_ }
        Fail "$Disp 编译失败（日志 编译maplibre-日志\$b.log）"
    }

    Say "  安装到 install-$b …"
    if (Test-Path $inst) {
        $old = Join-Path $Root ('【删除】\install-' + $b + '-旧-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Force -Path (Split-Path $old) | Out-Null
        Move-Item $inst $old
    }
    $ErrorActionPreference = 'Continue'
    & cmake --install $bd 2>&1 | ForEach-Object { Add-Content -Path $log -Value "$_" -Encoding UTF8 }
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($rc -ne 0) { Fail "$Disp 安装失败（日志 编译maplibre-日志\$b.log）" }
    foreach ($must in @('bin\QMapLibre.dll', 'qml\MapLibre\qmldir', 'lib\cmake\QMapLibre\QMapLibreConfig.cmake')) {
        if (-not (Test-Path (Join-Path $inst $must))) { Fail "$Disp 安装后缺少 $must" }
    }
    $sizeMB = [math]::Round(((Get-ChildItem $inst -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB, 1)
    Say "  [√] $Disp 完成：$total 步，用时 $(Fmt ((Get-Date) - $tb).TotalSeconds)，install-$b 共 $sizeMB MB" 'Green'
    Say ''
}
Say "全部完成，总用时 $(Fmt ((Get-Date) - $T0).TotalSeconds)。" 'Green'
Say '下一步：运行 编译.bat（会把两套地图库一起放进程序目录）。'
exit 0
