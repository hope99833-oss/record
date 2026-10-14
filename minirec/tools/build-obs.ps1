# MiniRec：构建 libobs 与白名单插件（不构建 OBS 自带 Qt 前端）
#
# 前置：先运行 tools\prepare-obs.ps1 把源码与依赖放好
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\build-obs.ps1             # 配置 + 编译 + 安装
#   powershell -ExecutionPolicy Bypass -File tools\build-obs.ps1 -ConfigureOnly
#
# 产物：D:\dev\obs-prefix（libobs 的 CMake 包与头文件，供 minirec 使用）
#       D:\dev\build-obs\rundir\RelWithDebInfo\bin\64bit（运行时目录，打包脚本从这里取）

param(
  # C:\mrlink 是指向本工程内 obs\ 目录的目录联接（junction）：
  # 构建工具会写海量中间文件，短路径既能避开 MAX_PATH 限制，又能保证产物落回工程目录。
  [string]$ObsSource = "C:\mrlink\src\obs-studio-32.2.2",
  [string]$BuildDir = "C:\mrlink\build-obs",
  [string]$Prefix = "C:\mrlink\prefix",
  [ValidateSet("RelWithDebInfo", "Release", "Debug", "MinSizeRel")]
  [string]$Configuration = "RelWithDebInfo",
  [switch]$ConfigureOnly,
  [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"

$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { throw "找不到 CMake：$cmake" }
if (-not (Test-Path (Join-Path $ObsSource "CMakeLists.txt"))) { throw "找不到 OBS 源码：$ObsSource（先跑 prepare-obs.ps1）" }

# 本地已放好的预编译依赖（prepare-obs.ps1 解压到 <源码>/.deps 下）
$depsDir = Join-Path $ObsSource ".deps"
$prebuiltDir = Join-Path $depsDir "obs-deps-2026-07-15-x64"
$qt6Dir = Join-Path $depsDir "obs-deps-qt6-2026-07-15-x64"
if (-not (Test-Path (Join-Path $prebuiltDir "share\obs-deps\VERSION"))) { throw "obs-deps 未就位：$prebuiltDir" }
if (-not (Test-Path (Join-Path $qt6Dir "share\obs-deps\VERSION"))) { throw "obs-deps-qt6 未就位：$qt6Dir" }

# OBS 依赖判定里用 if(EXISTS ...) 拼路径，反斜杠会被当成转义字符导致判定失败，
# 传给 CMake 的路径统一转成正斜杠
$prebuiltDirCmake = $prebuiltDir -replace '\\', '/'
$qt6DirCmake = $qt6Dir -replace '\\', '/'

# 录屏链路需要的 target。注意：OBS 的 install 规则覆盖所有已配置目标，
# 少编一个（例如 libobs-opengl）就会让 `cmake --install` 中断，
# 因此这里默认整包编译；"轻量"体现在最终打包（packaging\package.ps1 只取下面这些）。
$targets = @(
  "libobs", "libobs-d3d11", "libobs-winrt",
  "win-capture", "win-wasapi", "win-dshow",
  "obs-ffmpeg", "obs-outputs", "obs-x264", "obs-nvenc", "obs-qsv11"
)

Write-Host "==== 1/3 配置 ===="
$configureArgs = @(
  "-S", $ObsSource,
  "-B", $BuildDir,
  "-G", "Visual Studio 18 2026",
  "-A", "x64",
  "-DENABLE_FRONTEND=OFF",       # 不构建 OBS 自带 Qt 前端（我们用自研宿主）
  "-DENABLE_SCRIPTING=OFF",
  "-DENABLE_BROWSER=OFF",        # 无 CEF
  "-DENABLE_WEBSOCKET=OFF",
  "-DENABLE_VLC=OFF",
  "-DENABLE_AJA=OFF",
  "-DENABLE_DECKLINK=OFF",
  "-DENABLE_VIRTUALCAM=OFF",
  "-DENABLE_VST=OFF",
  "-DENABLE_NVAFX=OFF",
  "-DENABLE_LIBFDK=OFF",
  "-DBUILD_TESTING=OFF",
  # 本工程不做游戏捕获，跳过 32 位 graphics-hook / get-graphics-offsets / inject-helper 交叉构建
  # （该开关由 cmake/windows/architecture.cmake 提供，默认 OFF，不影响 OBS 原生行为）
  "-DMINIREC_NO_X86=ON",
  # 依赖已手工就位，跳过 OBS 自带的版本比对/hash 校验/按需下载
  # （该开关由 cmake/windows/buildspec.cmake 提供，默认 OFF，不影响 OBS 原生行为）
  "-DMINIREC_VENDORED_DEPS=ON",
  # 依赖已在本地 .deps 下就位：显式给出搜索路径；同时把预编译包 hash 直接作为变量传入，
  # 让 OBS 的 CMake 认为依赖"已就绪"，避免它再去下载（本机到 GitHub 单连接只有 ~40KB/s）。
  "-DCMAKE_PREFIX_PATH=$prebuiltDirCmake;$qt6DirCmake",
  "-DOBS_DEPENDENCY_prebuilt_x64_HASH=6f90e9598fa10cff5ad23cdcfae49b87868c07bf896b02cd464582b4ce2f2ba9",
  "-DOBS_DEPENDENCY_qt6_x64_HASH=7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579",
  "-DCMAKE_INSTALL_PREFIX=$Prefix"
)

if ($Reconfigure -and (Test-Path $BuildDir)) {
  Write-Host "[清理] 删除旧的构建目录 $BuildDir"
  Remove-Item $BuildDir -Recurse -Force
}

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败（退出码 $LASTEXITCODE）" }

if ($ConfigureOnly) { Write-Host "仅配置完成"; return }

Write-Host "==== 2/3 编译（整包，保证 install 规则可完整执行） ===="
& $cmake --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "编译失败（退出码 $LASTEXITCODE）" }

Write-Host "==== 3/3 安装到 $Prefix ===="
& $cmake --install $BuildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "安装失败（退出码 $LASTEXITCODE）" }

# libobs 的头文件、obs.lib 导入库与 libobsConfig.cmake 属于 Development 组件
# （OBS 的 install 规则标了 EXCLUDE_FROM_ALL），必须单独再装一次，minirec 才找得到 find_package(libobs)。
& $cmake --install $BuildDir --config $Configuration --component Development
if ($LASTEXITCODE -ne 0) { throw "安装 Development 组件失败（退出码 $LASTEXITCODE）" }

Write-Host ""
Write-Host "完成。"
Write-Host "  运行时目录：$BuildDir\rundir\$Configuration\bin\64bit"
Write-Host "  libobs 安装：$Prefix"
Write-Host "  打包白名单：$($targets -join ', ')"
