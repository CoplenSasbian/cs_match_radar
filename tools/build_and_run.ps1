# 构建并启动 CsMatchRadar。
#
# 本工程是普通的 unpackaged 桌面程序（WindowsPackageType=None），
# 不需要 MSIX 注册：输出目录即运行目录，exe 可直接双击或在 VS 里 F5。
# 脚本只负责构建 + 校验依赖 DLL + 启动。
#
# 用法：
#   pwsh -File tools\build_and_run.ps1               # 构建 + 启动
#   pwsh -File tools\build_and_run.ps1 -NoLaunch     # 只构建
#   pwsh -File tools\build_and_run.ps1 -Rebuild      # 先清理再构建

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64', 'Win32', 'ARM64')]
    [string]$Platform = 'x64',

    [switch]$Rebuild,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot          # cs_match_radar\
$appProj = Join-Path $root 'CsMatchRadar\CsMatchRadar.vcxproj'
$dllProj = Join-Path $root '5eApi\5eApi.vcxproj'
# unpackaged：exe 与依赖 DLL 同目录，输出目录即运行目录。
$outDir = Join-Path $root "CsMatchRadar\$Platform\$Configuration\CsMatchRadar"
$exe = Join-Path $outDir 'App1.exe'

# 找到 MSBuild。
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "找不到 vswhere.exe：$vswhere" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { throw "找不到 Visual Studio 安装" }
$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "找不到 MSBuild：$msbuild" }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$common = @("/p:Configuration=$Configuration", "/p:Platform=$Platform", '/v:minimal', '/nologo', '/m:1')

function Invoke-Build([string]$proj, [string]$label) {
    Write-Host "==== 构建 $label ====" -ForegroundColor Cyan
    & $msbuild $proj @common "/t:$target"
    if ($LASTEXITCODE -ne 0) { throw "$label 构建失败（exit=$LASTEXITCODE）" }
}

# 5eApi 是独立的 DLL 工程；应用把同一份源码直接编入，因此两者都要能编译。
Invoke-Build $dllProj '5eApi (DLL)'
Invoke-Build $appProj 'CsMatchRadar (App)'

if (-not (Test-Path $exe)) { throw "未找到生成的 exe：$exe" }

# 关闭正在运行的实例，否则文件被占用会导致覆盖失败。
Get-Process -Name 'App1' -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "关闭正在运行的实例 pid=$($_.Id)"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 2

# 校验依赖的 DLL 都已就位且不比产物旧。
#
# unpackaged 模式下 exe 直接从输出目录运行，DLL 必须与它同目录且同版本；
# 这里检查是为了尽早发现"改了 DLL 但没重新构建"的错配
# （那种错配在启动时表现为 0xC0000139 Entry Point Not Found）。
$required = @('5eApi.dll', 'PerfectWorldApi.dll')
$outputs = @{
    '5eApi.dll'         = Join-Path $root "5eApi\$Platform\$Configuration\5eApi.dll"
    'PerfectWorldApi.dll' = Join-Path $root "PerfectWorldApi\$Platform\$Configuration\PerfectWorldApi.dll"
}
foreach ($dll in $required) {
    $inOutput = Join-Path (Split-Path $exe -Parent) $dll
    if (-not (Test-Path $inOutput)) {
        throw "输出目录缺少 $dll（期望 $inOutput）"
    }
    $buildPath = $outputs[$dll]
    if (-not (Test-Path $buildPath)) { continue }
    $inOut = Get-Item $inOutput
    $build = Get-Item $buildPath
    if ($inOut.LastWriteTime -lt $build.LastWriteTime) {
        throw "$dll 未更新（输出目录 $($inOut.LastWriteTime) 早于产物 $($build.LastWriteTime)）"
    }
}
Write-Host "依赖 DLL 校验通过（$($required -join ', ')）" -ForegroundColor Green

# 清理旧的包注册（历史遗留）。
# 本工程已改为 unpackaged，不再需要注册；但之前注册过的同名包会残留在系统里，
# 容易在"开始"菜单里留下无法启动的旧入口，这里顺手清掉。
$stale = Get-AppxPackage -Name '2fc24ecc-1c57-41a0-8cdc-b0053a5fd463' -ErrorAction SilentlyContinue
if ($stale) {
    Write-Host "清理历史包注册：$($stale.InstallLocation)"
    $stale | Remove-AppxPackage -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

if ($NoLaunch) {
    Write-Host "已跳过启动（-NoLaunch）。可直接运行：$exe" -ForegroundColor Yellow
    return
}

Write-Host "==== 启动应用 ====" -ForegroundColor Cyan
Start-Process $exe

Start-Sleep -Seconds 8
$proc = Get-Process -Name 'App1' -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($proc) {
    Write-Host "运行中：pid=$($proc.Id)  窗口='$($proc.MainWindowTitle)'" -ForegroundColor Green
} else {
    throw "应用未出现窗口，请检查 $exe 所在目录是否完整（缺 DLL / 缺 Windows App Runtime 都会导致启动失败）。"
}
