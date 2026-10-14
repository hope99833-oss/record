# MiniRec

轻量级 Windows 录屏工具，基于 [OBS Studio](https://obsproject.com/) 的 libobs 引擎与 Qt6 自研极简宿主界面。不依赖 OBS 完整前端，只构建录屏链路真正需要的模块，追求「小、快、稳」。

## 功能特性

- **三种捕获模式**：显示器 / 窗口 / 自选区域
- **系统声音 + 麦克风**：可分别开关并选择输入设备
- **智能编码器**：优先 NVIDIA NVENC / Intel QSV 硬件编码，自动回退到 x264 软件编码
- **鼠标光标录制 + 点击特效**：光圈大小、颜色、持续时间可调
- **摄像头画中画**：四角位置可选、宽度可调
- **倒计时启动**、**暂停 / 继续**
- **崩溃安全录制**：先落盘 MKV，可选自动转封装 MP4，异常退出时上次录像仍可抢救
- **全局热键**：默认 `F9` 开始/停止、`F10` 暂停
- **系统托盘**：关闭窗口收起到托盘，后台常驻
- **单实例**：重复启动自动唤醒已有窗口
- **崩溃恢复检测**：上次异常退出时提示检查残留文件

## 依赖

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| OBS Studio (libobs) | 32.2.2 | 录屏引擎 |
| obs-deps | 2026-07-15 | FFmpeg / x264 / jansson 等预编译依赖 |
| Qt6 | 6.x（obs-deps 自带） | 界面 |
| CMake | ≥ 3.28 | 构建 |
| Visual Studio | 2026（x64 工具链） | 编译 |
| Windows | 10 / 11 x64 | 运行 |

## 目录结构

```
record/
├── minirec/               # MiniRec 源码（本仓库唯一提交的内容）
│   ├── src/
│   │   ├── app/           # 配置 / 托盘 / 热键 / 单实例
│   │   ├── core/          # libobs 封装 / 场景 / 编码 / 输出 / 区域选择 / 鼠标特效
│   │   ├── ui/            # 主窗口 / 设置 / 悬浮控制条 / 主题
│   │   └── main.cpp       # 入口
│   ├── resources/         # 图标 / 应用清单
│   ├── tools/             # 依赖获取 / OBS 就位 / libobs 构建脚本
│   └── packaging/         # 打包脚本
└── obs/                   # OBS 源码 + 依赖 + 构建产物（git 忽略，由脚本重新生成）
```

> `obs/` 目录（约 2.6 GB，含第三方源码、预编译依赖和编译产物）**不纳入版本库**，
> 通过下面的脚本按需重新下载生成。

## 构建

### 0. 环境准备

安装以下组件并确保在 PATH 中可用：

- [Visual Studio 2026](https://visualstudio.microsoft.com/)（勾选「使用 C++ 的桌面开发」）
- [Git](https://git-scm.com/)
- PowerShell 5.1+

### 1. 获取源码

```powershell
git clone https://github.com/hope99833-oss/record.git
cd record
```

### 2. 下载 OBS 源码与依赖

```powershell
powershell -ExecutionPolicy Bypass -File minirec\tools\fetch-sources.ps1
```

默认下载到 `D:\dev\dl`，包含 obs-studio 32.2.2 源码、obs-deps / obs-deps-qt6 预编译包，
以及 libdshowcapture 等 submodule 归档。下载采用多段并发 + 校验，可加 `-SkipQt` 跳过 Qt6。

### 3. 解压并就位（源码 + 依赖 + 短路径联接）

```powershell
powershell -ExecutionPolicy Bypass -File minirec\tools\prepare-obs.ps1
```

- 把源码解压到 `obs\src\obs-studio-32.2.2`
- 把依赖解压到 `<源码>\.deps\obs-deps-*-x64`
- 放置 submodule
- 创建目录联接 `C:\mrlink`（短路径，避免 MSVC 的 MAX_PATH 限制）

### 4. 构建 libobs

```powershell
powershell -ExecutionPolicy Bypass -File minirec\tools\build-obs.ps1
```

编译 libobs 与白名单插件，安装到 `C:\mrlink\prefix`。仅需配置可加 `-ConfigureOnly`。

### 5. 构建 MiniRec

```powershell
cmake -S minirec -B obs\build-minirec -G "Visual Studio 18 2026" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\mrlink\prefix"
cmake --build obs\build-minirec --config RelWithDebInfo
```

产物 `minirec.exe` 输出到 `obs\build-minirec\rundir`。Qt6 目录由 CMake 依据
`obs\src\obs-studio-32.2.2\.deps` 自动定位。

### 6. 打包（可选）

```powershell
powershell -ExecutionPolicy Bypass -File minirec\packaging\package.ps1
```

按白名单裁剪插件、补齐依赖 DLL，最终输出到 `dist\MiniRec`（自检关键文件是否齐全）。

## 使用

1. 启动 `minirec.exe`，主窗口常驻，关闭窗口会收起到托盘
2. 选择捕获模式（显示器 / 窗口 / 区域），设置音频、画质、摄像头画中画、鼠标特效
3. 点击「开始录制」（或按全局热键 `F9`），倒计时结束后开始录制
4. 录制中按 `F10` 暂停/继续，`F9` 停止
5. 录制结束后按配置自动转封装 MP4，录像默认保存在 `%USERPROFILE%\Videos\MiniRec`

## 配置与日志

| 项目 | 位置 |
| --- | --- |
| 配置文件 | `%APPDATA%\MiniRec\` |
| 运行日志 | `%APPDATA%\MiniRec\logs\minirec-YYYYMMDD.log` |
| 启动日志 | `<exe 同级>\minirec-startup.log`（GUI 双击无反应时从这里定位） |
| 默认录像目录 | `%USERPROFILE%\Videos\MiniRec` |

日志超过 7 天自动清理。

## 许可证

本项目作为 OBS Studio 的衍生程序，链接 libobs 需遵循其开源许可。许可证以最终选择的
[GPL 许可](https://www.gnu.org/licenses/)为准，具体条款请以仓库后续添加的 `LICENSE` 文件为准。
