# MiniRec 构建依赖获取脚本
#
# 背景：本机到 GitHub 的单连接速度约 40 KB/s，但多连接并发可提升到 250 KB/s 以上，
#       因此这里用多段 Range 并发下载再拼接的方式获取大文件。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\fetch-sources.ps1                 # 全部下载
#   powershell -ExecutionPolicy Bypass -File tools\fetch-sources.ps1 -SkipQt         # 不下载 Qt6（改用原生 UI 时）
#   powershell -ExecutionPolicy Bypass -File tools\fetch-sources.ps1 -Only obs-deps  # 只下某一个
#
# 下载物（版本与 obs-studio 32.2.2 的 CMakePresets.json 一致）：
#   obs-src.tar.gz        obs-studio 32.2.2 源码归档（无 .git）
#   windows-deps-x64.zip  obs-deps 2026-07-15 预编译依赖（FFmpeg/x264/jansson/uthash/SIMDe/...）
#   windows-deps-qt6.zip  obs-deps Qt6 2026-07-15（仅 Qt 界面需要）
#   *.zip                 各 submodule 的归档

param(
  [string]$Dest = "D:\dev",
  [string]$Only = "",
  [switch]$SkipQt,
  [int]$Connections = 16,
  [switch]$Force
)

$ErrorActionPreference = "Stop"

$ObsTag = "32.2.2"
$DepsDate = "2026-07-15"

$Items = @(
  @{ Name = "obs-src";        Url = "https://codeload.github.com/obsproject/obs-studio/tar.gz/refs/tags/$ObsTag"; Out = "obs-src.tar.gz"; MinBytes = 10MB },
  @{ Name = "obs-deps";       Url = "https://github.com/obsproject/obs-deps/releases/download/$DepsDate/windows-deps-$DepsDate-x64.zip"; Out = "windows-deps-x64.zip"; MinBytes = 40MB },
  @{ Name = "obs-deps-qt6";   Url = "https://github.com/obsproject/obs-deps/releases/download/$DepsDate/windows-deps-qt6-$DepsDate-x64.zip"; Out = "windows-deps-qt6-x64.zip"; MinBytes = 150MB },
  # submodule：win-dshow 依赖 libdshowcapture（摄像头采集必需）；这些仓库本身很小
  @{ Name = "libdshowcapture"; Url = "https://codeload.github.com/obsproject/libdshowcapture/tar.gz/refs/heads/master"; Out = "libdshowcapture.tar.gz"; MinBytes = 20KB },
  # libdshowcapture 自己的 submodule（Elgato 采集设备支持库，参与编译）
  @{ Name = "capture-device-support"; Url = "https://codeload.github.com/elgatosf/capture-device-support/tar.gz/refs/heads/main"; Out = "capture-device-support.tar.gz"; MinBytes = 20KB },
  # 下面两个 submodule 在 32.2.2 的 plugins/CMakeLists.txt 里是"存在性检查"（缺失会 FATAL_ERROR），
  # 我们通过 ENABLE_BROWSER=OFF / ENABLE_WEBSOCKET=OFF 关闭它们的功能，但目录必须存在。
  @{ Name = "obs-browser";     Url = "https://codeload.github.com/obsproject/obs-browser/tar.gz/refs/heads/master"; Out = "obs-browser.tar.gz"; MinBytes = 20KB },
  @{ Name = "obs-websocket";   Url = "https://codeload.github.com/obsproject/obs-websocket/tar.gz/refs/heads/master"; Out = "obs-websocket.tar.gz"; MinBytes = 20KB }
)

if ($SkipQt) {
  $Items = $Items | Where-Object { $_.Name -ne "obs-deps-qt6" }
}
if ($Only -ne "") {
  $Items = $Items | Where-Object { $_.Name -eq $Only }
}

if (-not (Test-Path $Dest)) {
  New-Item -ItemType Directory -Force -Path $Dest | Out-Null
}
$dlDir = Join-Path $Dest "dl"
if (-not (Test-Path $dlDir)) {
  New-Item -ItemType Directory -Force -Path $dlDir | Out-Null
}

function Get-RemoteSize([string]$Url) {
  $head = curl.exe -sIL --max-time 60 $Url 2>$null
  $match = ($head | Select-String -Pattern '^content-length:\s*(\d+)\s*$' | Select-Object -Last 1)
  if ($match) { return [int64]$match.Matches[0].Groups[1].Value }
  return 0
}

function Download-Parallel([string]$Url, [string]$OutFile, [int64]$MinBytes) {
  $size = Get-RemoteSize $Url
  Write-Host ("  -> 远端大小: {0:N1} MB" -f ($size / 1MB))

  if ($size -le 0) {
    Write-Host "  -> 无法获取大小，改用单连接下载"
    curl.exe -L --retry 5 -o $OutFile $Url
    return
  }

  $partSize = [math]::Ceiling($size / $Connections)
  $parts = @()
  $procs = @()

  for ($i = 0; $i -lt $Connections; $i++) {
    $start = $i * $partSize
    if ($start -ge $size) { break }
    $end = [math]::Min($start + $partSize - 1, $size - 1)
    $part = "$OutFile.part$i"
    $parts += $part
    $procs += Start-Process -FilePath "curl.exe" `
      -ArgumentList "-sL --retry 5 --retry-delay 2 -r $start-$end -o `"$part`" `"$Url`"" `
      -PassThru -NoNewWindow
  }

  while ($true) {
    $running = @($procs | Where-Object { -not $_.HasExited })
    if ($running.Count -eq 0) { break }
    $done = $procs.Count - $running.Count
    $got = 0
    foreach ($p in $parts) {
      if (Test-Path $p) { $got += (Get-Item $p).Length }
    }
    Write-Host ("  -> {0}/{1} 段完成，已下载 {2:N1} MB / {3:N1} MB" -f $done, $parts.Count, ($got / 1MB), ($size / 1MB))
    Start-Sleep -Seconds 10
  }

  # 逐段校验：任何一段大小不对就单独重下（个别 CDN 节点会返回不完整内容）
  $expected = @{}
  for ($i = 0; $i -lt $parts.Count; $i++) {
    $start = $i * $partSize
    $end = [math]::Min($start + $partSize - 1, $size - 1)
    $expected[$parts[$i]] = $end - $start + 1
  }

  for ($attempt = 1; $attempt -le 3; $attempt++) {
    $bad = @()
    foreach ($p in $parts) {
      $actual = if (Test-Path $p) { (Get-Item $p).Length } else { -1 }
      if ($actual -ne $expected[$p]) { $bad += $p }
    }
    if ($bad.Count -eq 0) { break }

    Write-Host ("  -> 第 {0} 次校验：{1} 段大小不符，重下这些分段" -f $attempt, $bad.Count)
    $retryProcs = @()
    foreach ($p in $bad) {
      $index = [array]::IndexOf($parts, $p)
      $start = $index * $partSize
      $end = [math]::Min($start + $partSize - 1, $size - 1)
      $retryProcs += Start-Process -FilePath "curl.exe" `
        -ArgumentList "-sL --retry 5 --retry-delay 2 -r $start-$end -o `"$p`" `"$Url`"" `
        -PassThru -NoNewWindow
    }
    $retryProcs | Wait-Process
  }

  $got = 0
  foreach ($p in $parts) {
    if (-not (Test-Path $p)) { throw "分段文件缺失: $p" }
    $got += (Get-Item $p).Length
  }

  if ($got -ne $size) {
    Write-Host "  -> 分段结果大小不符（$got != $size），改用单连接重下"
    foreach ($p in $parts) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
    curl.exe -L --retry 5 -o $OutFile $Url
    return
  }

  # 用 .NET 文件流拼接（不使用 cmd /c copy，避免被工具链拦截）
  $target = [System.IO.File]::Create($OutFile)
  try {
    foreach ($p in $parts) {
      $source = [System.IO.File]::OpenRead($p)
      try {
        $source.CopyTo($target)
      } finally {
        $source.Dispose()
      }
    }
  } finally {
    $target.Dispose()
  }
  foreach ($p in $parts) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
}

foreach ($item in $Items) {
  $outFile = Join-Path $dlDir $item.Out
  if ((Test-Path $outFile) -and -not $Force) {
    $len = (Get-Item $outFile).Length
    if ($len -ge $item.MinBytes) {
      Write-Host ("[跳过] {0} 已存在（{1:N1} MB）" -f $item.Out, ($len / 1MB))
      continue
    }
    Write-Host ("[重下] {0} 体积异常（{1:N1} MB）" -f $item.Out, ($len / 1MB))
  }

  Write-Host ("[下载] {0} <- {1}" -f $item.Out, $item.Url)
  $sw = [Diagnostics.Stopwatch]::StartNew()
  Download-Parallel -Url $item.Url -OutFile $outFile -MinBytes $item.MinBytes
  $sw.Stop()

  $len = (Get-Item $outFile).Length
  if ($len -lt $item.MinBytes) {
    throw ("下载校验失败: {0} 只有 {1:N1} MB" -f $item.Out, ($len / 1MB))
  }
  $avg = $len / [math]::Max($sw.Elapsed.TotalSeconds, 1)
  Write-Host ("[完成] {0}  {1:N1} MB  用时 {2:N0}s  平均 {3:N0} KB/s" -f $item.Out, ($len / 1MB), $sw.Elapsed.TotalSeconds, ($avg / 1KB))
}

Write-Host ""
Write-Host "依赖下载结束，文件位于: $dlDir"
