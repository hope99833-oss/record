# MiniRec：打包脚本（白名单裁剪 + 原样保留 OBS 运行时目录结构）
#
# 目录结构（必须与 libobs 的相对路径查找保持一致，勿手工调整）：
#   <输出根>/
#     bin/64bit/minirec.exe            <- 主程序（与 obs.dll 同目录）
#     bin/64bit/obs.dll / libobs-d3d11.dll / libobs-winrt.dll / Qt6*.dll ...
#     bin/64bit/platforms/qwindows.dll <- Qt 平台插件
#     obs-plugins/64bit/*.dll          <- 采集/编码插件（按白名单裁剪）
#     data/libobs/...                  <- libobs 着色器（default.effect 等）
#     data/obs-plugins/<模块>/...      <- 白名单插件的数据文件
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File packaging\package.ps1

param(
  [string]$ObsRunDir = "C:\mrlink\build-obs\rundir\RelWithDebInfo",
  [string]$MinirecRunDir = "C:\mrlink\build-minirec\rundir",
  # 默认用 obs-deps 自带的 Qt6（与 libobs 同一套工具链）
  [string]$QtDir = "C:\mrlink\src\obs-studio-32.2.2\.deps\obs-deps-qt6-2026-07-15-x64",
  # 预编译依赖里的运行时 DLL（ffmpeg / libcurl / zlib / x264 等）
  [string]$ObsDepsBin = "C:\mrlink\src\obs-studio-32.2.2\.deps\obs-deps-2026-07-15-x64\bin",
  [string]$OutDir = "$PSScriptRoot\..\..\dist\MiniRec"
)

$ErrorActionPreference = "Stop"

# 录屏链路真正需要的插件（其余插件不进入发布包，这是"轻量"的最后一层裁剪）
$pluginWhitelist = @(
  "win-capture",   # 显示器/窗口采集
  "win-wasapi",    # 系统声音 / 麦克风
  "win-dshow",     # 摄像头
  "obs-ffmpeg",    # AAC 编码 + ffmpeg_muxer 输出
  "obs-outputs",   # 输出基础设施（flv/mp4 相关）
  "obs-x264",      # 软件编码兜底
  "obs-nvenc",     # NVIDIA 硬件编码
  "obs-qsv11"      # Intel 硬件编码
)

$binDir = Join-Path $OutDir "bin\64bit"

# minirec.exe 启动时会把工作目录切到 bin\64bit，程序还在跑时该目录删不掉。
# 这种情况下退化为原地覆盖（文件本身一般没被独占），并提示残留文件不会被清理。
if (Test-Path $OutDir) {
  $cleaned = $false
  for ($i = 1; $i -le 3 -and -not $cleaned; $i++) {
    try {
      Remove-Item $OutDir -Recurse -Force -ErrorAction Stop
      $cleaned = $true
    } catch {
      Start-Sleep -Seconds 2
    }
  }
  if (-not $cleaned) {
    Write-Warning "输出目录被占用，改为原地覆盖：$OutDir"
    Write-Warning "多半是 minirec.exe 还在运行（它会把工作目录切到 bin\64bit）。请关掉程序后再打包，否则上一版多余的文件不会被清理。"
  }
}
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

function Copy-Tree([string]$from, [string]$to, [string]$what) {
  if (-not (Test-Path $from)) { Write-Host "[跳过] 不存在：$from ($what)"; return }
  New-Item -ItemType Directory -Force -Path $to | Out-Null
  Copy-Item (Join-Path $from "*") $to -Recurse -Force
  Write-Host "[拷贝] $what -> $to"
}

# ---------------------------------------------------------------- 1. OBS 运行时
# bin：跳过 pdb（几十 MB 的调试符号）。
# 注意不要把 *-test.exe 过滤掉：obs-nvenc / obs-qsv11 在 obs_module_load 阶段会
# CreateProcess 拉起 obs-nvenc-test.exe / obs-qsv-test.exe 探测驱动，缺了就会
# "Failed to initialize module 'obs-nvenc.dll'"，硬件编码会静默退回 x264 CPU 编码。
$srcBin = Join-Path $ObsRunDir "bin\64bit"
foreach ($f in Get-ChildItem $srcBin -File -ErrorAction SilentlyContinue) {
  if ($f.Extension -eq ".pdb") { continue }
  Copy-Item $f.FullName $binDir -Force
}
Write-Host "[拷贝] libobs 运行时（obs.dll / obs-ffmpeg-mux.exe / *-test.exe 等） -> $binDir"

# 插件 DLL：只取白名单
$srcPlugins = Join-Path $ObsRunDir "obs-plugins\64bit"
$dstPlugins = Join-Path $OutDir "obs-plugins\64bit"
New-Item -ItemType Directory -Force -Path $dstPlugins | Out-Null
foreach ($name in $pluginWhitelist) {
  $src = Join-Path $srcPlugins "$name.dll"
  if (Test-Path $src) {
    Copy-Item $src $dstPlugins -Force
  } else {
    Write-Warning "白名单插件未构建：$name.dll"
  }
}
Write-Host "[拷贝] 插件白名单 -> $dstPlugins（$($pluginWhitelist.Count) 个）"

# libobs 自带数据（着色器 default.effect 等）必须完整保留
Copy-Tree (Join-Path $ObsRunDir "data\libobs") (Join-Path $OutDir "data\libobs") "data\libobs（着色器）"

# 插件数据目录：只取白名单插件
foreach ($name in $pluginWhitelist) {
  $src = Join-Path $ObsRunDir "data\obs-plugins\$name"
  if (Test-Path $src) {
    Copy-Tree $src (Join-Path $OutDir "data\obs-plugins\$name") "data\obs-plugins\$name"
  }
}

# ---------------------------------------------------------------- 2. 主程序
$minirecExe = Join-Path $MinirecRunDir "minirec.exe"
if (-not (Test-Path $minirecExe)) { throw "找不到 minirec.exe：$minirecExe（先构建 minirec）" }
Copy-Item $minirecExe $binDir -Force
Write-Host "[拷贝] minirec.exe -> $binDir"

# ---------------------------------------------------------------- 3. Qt 运行时
if (Test-Path $QtDir) {
  $qtBin = Join-Path $QtDir "bin"
  if (Test-Path $qtBin) {
    foreach ($pattern in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll")) {
      $src = Join-Path $qtBin $pattern
      if (Test-Path $src) { Copy-Item $src $binDir -Force; Write-Host "[拷贝] $pattern" }
    }
  }

  $qtPlatforms = Join-Path $QtDir "plugins\platforms"
  if (Test-Path $qtPlatforms) {
    # 只保留 windows 平台插件
    New-Item -ItemType Directory -Force -Path (Join-Path $binDir "platforms") | Out-Null
    foreach ($name in @("qwindows.dll", "qminimal.dll", "qoffscreen.dll")) {
      $src = Join-Path $qtPlatforms $name
      if (Test-Path $src) { Copy-Item $src (Join-Path $binDir "platforms") -Force }
    }
    if (-not (Test-Path (Join-Path $binDir "platforms\qwindows.dll"))) {
      Write-Warning "未拷贝到 qwindows.dll，程序将无法启动"
    }
  } else {
    Write-Warning "未找到 Qt 平台插件目录（platforms）：$qtPlatforms"
  }

  # Qt6 的窗口样式依赖 Qt6Svg（用 Fusion/Windows 样式时可能需要）
  $qtImageFormats = Join-Path $QtDir "plugins\imageformats"
  if (Test-Path $qtImageFormats) {
    New-Item -ItemType Directory -Force -Path (Join-Path $binDir "imageformats") | Out-Null
    foreach ($name in @("qico.dll", "qsvg.dll")) {
      $src = Join-Path $qtImageFormats $name
      if (Test-Path $src) { Copy-Item $src (Join-Path $binDir "imageformats") -Force }
    }
  }
  $qtSvgDll = Join-Path $qtBin "Qt6Svg.dll"
  if (Test-Path $qtSvgDll) { Copy-Item $qtSvgDll $binDir -Force }
} else {
  Write-Warning "未找到 Qt 目录：$QtDir"
}

# ---------------------------------------------------------------- 4. VC 运行时（保证干净机器可运行）
$crtCandidates = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1
if ($crtCandidates) {
  foreach ($dll in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140_1.dll", "msvcp140_2.dll")) {
    $src = Join-Path $crtCandidates.FullName $dll
    if (Test-Path $src) { Copy-Item $src $binDir -Force }
  }
  Write-Host "[拷贝] VC 运行时（来自 $($crtCandidates.FullName)）"
} else {
  Write-Warning "未找到 VC 运行时重分发包，目标机器需要自行安装 VC++ 2015-2022 运行库"
}

# ---------------------------------------------------------------- 5. 依赖 DLL 补齐
# OBS 的 _bundle_dependencies 只挂在自带前端目标（obs-studio.exe）上，
# 本工程不构建前端，所以 zlib / ffmpeg / libcurl / x264 这些依赖 DLL 不会被自动拷贝。
# 这里自己扫导入表（含传递依赖），从 obs-deps、Qt、VC 运行时分发里补齐到 bin\64bit。
$searchDirs = @()
if (Test-Path $ObsDepsBin) { $searchDirs += $ObsDepsBin } else { Write-Warning "未找到 obs-deps\bin：$ObsDepsBin" }
$qtBinDir = Join-Path $QtDir "bin"
if (Test-Path $qtBinDir) { $searchDirs += $qtBinDir }
if ($crtCandidates) { $searchDirs += $crtCandidates.FullName }

$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1

if (-not $dumpbin) {
  Write-Warning "未找到 dumpbin.exe，跳过依赖 DLL 补齐（程序可能报缺少 zlib.dll 等）"
} elseif ($searchDirs.Count -eq 0) {
  Write-Warning "没有任何依赖搜索目录，跳过依赖 DLL 补齐"
} else {
  function Get-DependentNames([string]$path) {
    $names = @()
    $inList = $false
    foreach ($line in (& $dumpbin.FullName /nologo /dependents $path 2>$null)) {
      $t = "$line".Trim()
      if ($t -match "^Image has the following (delay load )?dependencies") { $inList = $true; continue }
      if ($inList) {
        if ($t -match "^[A-Za-z0-9_.\-]+\.dll$") { $names += $t; continue }
        if ($t -ne "") { $inList = $false }
      }
    }
    return $names
  }

  $scanDirs = @($binDir, (Join-Path $OutDir "obs-plugins\64bit"))
  $addedDeps = @()
  # 每轮扫描后可能出现新的依赖，循环到不再新增为止
  while ($true) {
    $present = @{}
    $targets = @()
    foreach ($d in $scanDirs) {
      foreach ($f in Get-ChildItem $d -File -ErrorAction SilentlyContinue) {
        $present[$f.Name.ToLower()] = $true
        if ($f.Extension -eq ".dll" -or $f.Extension -eq ".exe") { $targets += $f }
      }
    }

    $addedThisRound = 0
    foreach ($t in $targets) {
      foreach ($dep in (Get-DependentNames $t.FullName)) {
        $key = $dep.ToLower()
        if ($present.ContainsKey($key)) { continue }
        $src = $null
        foreach ($sd in $searchDirs) {
          $cand = Join-Path $sd $dep
          if (Test-Path $cand) { $src = $cand; break }
        }
        # 找不到的多半是系统 DLL（kernel32/user32/...），忽略即可
        if ($src) {
          Copy-Item $src $binDir -Force
          $present[$key] = $true
          $addedDeps += $dep
          $addedThisRound++
        }
      }
    }

    if ($addedThisRound -eq 0) { break }
  }

  if ($addedDeps.Count -gt 0) {
    Write-Host ("[拷贝] 依赖 DLL（{0} 个）：{1}" -f $addedDeps.Count, (($addedDeps | Sort-Object -Unique) -join ", "))
  } else {
    Write-Host "[拷贝] 依赖 DLL：无需补充"
  }
}

# ---------------------------------------------------------------- 6. 自检
$required = @(
  "bin\64bit\minirec.exe",
  "bin\64bit\obs.dll",
  "bin\64bit\libobs-d3d11.dll",
  "bin\64bit\platforms\qwindows.dll",
  # 来自 obs-deps 的运行时依赖（不是 OBS 自己构建的，最容易被漏掉）
  "bin\64bit\zlib.dll",
  "bin\64bit\libcurl.dll",
  # 硬件编码模块的驱动探测辅助进程，缺了会让 NVENC/QSV 静默失效
  "bin\64bit\obs-nvenc-test.exe",
  "bin\64bit\obs-qsv-test.exe",
  "data\libobs\default.effect",
  "obs-plugins\64bit\win-capture.dll",
  "obs-plugins\64bit\win-wasapi.dll",
  "obs-plugins\64bit\win-dshow.dll",
  "obs-plugins\64bit\obs-ffmpeg.dll",
  "obs-plugins\64bit\obs-x264.dll"
)

$missing = @()
foreach ($item in $required) {
  if (-not (Test-Path (Join-Path $OutDir $item))) { $missing += $item }
}

$muxFound = @(Get-ChildItem $OutDir -Recurse -Filter "obs-ffmpeg-mux.exe" -ErrorAction SilentlyContinue)
if ($muxFound.Count -eq 0) { $missing += "obs-ffmpeg-mux.exe（本地录像封装子进程）" }

$sizeMb = [math]::Round((Get-ChildItem $OutDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
$fileCount = (Get-ChildItem $OutDir -Recurse -File).Count

Write-Host ""
Write-Host "==== 打包结果 ===="
Write-Host "  输出目录 : $OutDir"
Write-Host "  文件数   : $fileCount"
Write-Host "  体积     : $sizeMb MB"
Write-Host "  启动入口 : $binDir\minirec.exe"

if ($missing.Count -gt 0) {
  Write-Warning ("缺少关键文件：`n  - " + ($missing -join "`n  - "))
  exit 1
}

Write-Host "  自检     : 通过"
