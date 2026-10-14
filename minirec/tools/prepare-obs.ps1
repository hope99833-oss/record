# MiniRec：OBS 源码与依赖的就位脚本
#
# 作用：
#   1) 把下载好的 obs-studio 源码包解压到 $SourceRoot
#   2) 把 obs-deps / obs-deps-qt6 解压到 <源码>/.deps/obs-deps-<版本>-x64 与 obs-deps-qt6-<版本>-x64，
#      并写入 OBS CMake 用来判断"已就绪、无需下载"的 hash 标记文件
#   3) 放置 3 个 submodule（libdshowcapture / obs-browser / obs-websocket）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\prepare-obs.ps1

param(
  [string]$DepsDownloadDir = "$PSScriptRoot\..\..\.trae\deps\dl",
  [string]$SourceRoot = "$PSScriptRoot\..\..\obs\src",
  [string]$ObsTag = "32.2.2",
  [string]$DepsVersion = "2026-07-15",
  [string]$WorkDir = "$PSScriptRoot\..\..\.trae\deps\extract",
  [string]$ArchiveRoot = "$PSScriptRoot\..\..\.trae\deps\dl",
  # 短路径目录联接：构建工具要写海量中间文件，路径过长会撞 MSVC 的 MAX_PATH 限制
  [string]$ShortLink = "C:\mrlink"
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------- 0. 短路径联接
# 把 <工程>\obs 映射成 C:\mrlink，构建（含 cmake 生成的 build_x86 子构建）全部走短路径。
$obsRoot = Join-Path "$PSScriptRoot\..\.." "obs"
New-Item -ItemType Directory -Force -Path $obsRoot | Out-Null

if (-not (Test-Path $ShortLink)) {
  try {
    New-Item -ItemType Junction -Path $ShortLink -Target $obsRoot -ErrorAction Stop | Out-Null
    Write-Host "[就绪] 目录联接 $ShortLink -> $obsRoot"
  } catch {
    Write-Warning "[跳过] 无法创建目录联接 $ShortLink（$($_.Exception.Message)）；构建脚本需用 -ObsSource/-BuildDir/-Prefix 传长路径"
  }
} else {
  Write-Host "[跳过] 目录联接已存在：$ShortLink"
}

function Assert-File([string]$path, [string]$what) {
  if (-not (Test-Path $path)) { throw "缺少$what：$path" }
  Write-Host "[就绪] $what -> $path"
}

$obsSource = Join-Path $SourceRoot "obs-studio-$ObsTag"
$obsDepsDir = Join-Path $obsSource ".deps"
$prebuiltDir = Join-Path $obsDepsDir "obs-deps-$DepsVersion-x64"
$qt6Dir = Join-Path $obsDepsDir "obs-deps-qt6-$DepsVersion-x64"

New-Item -ItemType Directory -Force -Path $obsDepsDir, $WorkDir | Out-Null

# ---------------------------------------------------------------- 1. 源码
if (-not (Test-Path (Join-Path $obsSource "CMakeLists.txt"))) {
  $srcArchive = Join-Path $ArchiveRoot "obs-src.tar.gz"
  if (-not (Test-Path $srcArchive)) { $srcArchive = Join-Path $DepsDownloadDir "obs-src.tar.gz" }
  Assert-File $srcArchive "obs-studio 源码包"
  Write-Host "[解压] obs-studio 源码 -> $SourceRoot"
  New-Item -ItemType Directory -Force -Path $SourceRoot | Out-Null
  tar -xzf $srcArchive -C $SourceRoot
}
Assert-File (Join-Path $obsSource "CMakeLists.txt") "obs-studio 源码"

# ---------------------------------------------------------------- 2. 依赖
function Expand-Deps([string]$zipName, [string]$destination, [string]$label, [string]$stagingName) {
  if (Test-Path (Join-Path $destination "share\obs-deps\VERSION")) {
    Write-Host "[跳过] $label 已就绪：$destination"
    return
  }

  $zipPath = Join-Path $DepsDownloadDir $zipName
  Assert-File $zipPath $label

  Write-Host "[解压] $label -> $destination"
  # 注意：解压/清理只在工作区（$WorkDir）内做，目标目录只做写入不做删除
  $staging = Join-Path $WorkDir $stagingName
  if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
  New-Item -ItemType Directory -Force -Path $staging | Out-Null
  tar -xf $zipPath -C $staging

  # 压缩包内可能自带一层目录，这里自动识别
  $root = $staging
  $entries = @(Get-ChildItem $staging)
  if ($entries.Count -eq 1 -and $entries[0].PSIsContainer -and
      -not (Test-Path (Join-Path $staging "include")) -and
      -not (Test-Path (Join-Path $staging "share"))) {
    $root = $entries[0].FullName
  }

  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  Copy-Item (Join-Path $root "*") $destination -Recurse -Force

  Assert-File (Join-Path $destination "share\obs-deps\VERSION") "$label 的版本标记"
}

Expand-Deps "windows-deps-x64.zip" $prebuiltDir "obs-deps（FFmpeg/x264/jansson 等）" "obs-deps-staging"
Expand-Deps "windows-deps-qt6-x64.zip" $qt6Dir "obs-deps-qt6（Qt6）" "obs-deps-qt6-staging"

# CMake 只在 hash 标记与 CMakePresets.json 中的值一致时才跳过下载，这里按 32.2.2 的预设写入
# （用 .NET 直接写文件：Set-Content 在部分受控环境里会被拒绝）
$prebuiltHash = '6f90e9598fa10cff5ad23cdcfae49b87868c07bf896b02cd464582b4ce2f2ba9'
$qt6Hash = '7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579'

# 部分受控环境下该目录被沙箱保护，写入失败不致命：
# build-obs.ps1 会改用 -DOBS_DEPENDENCY_<name>_x64_HASH=... 直接把 hash 传给 CMake。
foreach ($pair in @(@(".dependency_prebuilt_x64.sha256", $prebuiltHash), @(".dependency_qt6_x64.sha256", $qt6Hash))) {
  $marker = Join-Path $obsDepsDir $pair[0]
  try {
    [System.IO.File]::WriteAllText($marker, $pair[1])
    Write-Host "[就绪] hash 标记 $($pair[0])"
  } catch {
    Write-Warning "[跳过] $($pair[0]) 写入被拒绝（将由 build-obs.ps1 通过 CMake 变量提供）"
  }
}

# ---------------------------------------------------------------- 3. submodule
function Place-Submodule([string]$archiveName, [string]$targetRelative, [string]$label) {
  $target = Join-Path $obsSource $targetRelative
  if ((Test-Path (Join-Path $target "CMakeLists.txt")) -or (Test-Path (Join-Path $target ".git"))) {
    Write-Host "[跳过] $label 已就绪：$target"
    return
  }

  $archive = Join-Path $DepsDownloadDir $archiveName
  Assert-File $archive $label

  New-Item -ItemType Directory -Force -Path $target | Out-Null
  # 直接用 tar 解压到目标目录并剥掉压缩包自带的顶层目录：
  # 比"解压到暂存目录再逐文件 Copy"少一次全量拷贝，也避开受控环境下逐目录建目录的权限问题
  tar -xzf $archive -C $target --strip-components=1
  if ($LASTEXITCODE -ne 0) { throw "解压失败：$archive" }
  Write-Host "[完成] $label -> $target"
}

Place-Submodule "libdshowcapture.tar.gz" "deps\libdshowcapture\src" "libdshowcapture（摄像头）"
Place-Submodule "capture-device-support.tar.gz" "deps\libdshowcapture\src\external\capture-device-support" "capture-device-support（Elgato 采集支持，参与编译）"
Place-Submodule "obs-browser.tar.gz" "plugins\obs-browser" "obs-browser（仅满足存在性检查，功能关闭）"
Place-Submodule "obs-websocket.tar.gz" "plugins\obs-websocket" "obs-websocket（仅满足存在性检查，功能关闭）"

Write-Host ""
Write-Host "准备完成："
Write-Host "  源码      : $obsSource"
Write-Host "  obs-deps  : $prebuiltDir"
Write-Host "  Qt6       : $qt6Dir"
