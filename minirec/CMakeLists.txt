cmake_minimum_required(VERSION 3.28)

project(minirec VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------------------
# 依赖
#   libobs            : 由 obs-studio 构建安装到前缀（find_package(libobs) -> OBS::libobs）
#   Qt6::Widgets      : 托盘 / 主窗口 / 设置 / 悬浮控制条
# 配置示例（构建目录放短路径，避免 MSVC 的 MAX_PATH 问题）：
#   cmake -S . -B D:/dev/build/minirec -G "Visual Studio 18 2026" -A x64 ^
#         -DCMAKE_PREFIX_PATH="D:/dev/obs-prefix;D:/dev/obs-deps-qt6"
# ---------------------------------------------------------------------------
find_package(libobs QUIET)

# ---------------------------------------------------------------------------
# Qt：默认使用 obs-deps 自带的 Qt6。
#   本机 VS 2026 的 STL 已经移除 stdext::make_checked_array_iterator，
#   而 Qt 5.15.2 的 qcompilerdetection.h 仍在无条件引用它，Qt5 头文件过不了编译；
#   obs-deps 的 Qt6 与 libobs 出自同一套工具链，ABI 也一致。
#   若确实要用 Qt5（需配 MSVC 2019 工具链），传 -DMINIREC_QT_MAJOR=5。
# ---------------------------------------------------------------------------
set(MINIREC_QT_MAJOR 6 CACHE STRING "使用的 Qt 大版本（5 或 6）")
set(MINIREC_OBS_DEPS_QT6
    "${CMAKE_CURRENT_SOURCE_DIR}/../obs/src/obs-studio-32.2.2/.deps/obs-deps-qt6-2026-07-15-x64"
    CACHE PATH "obs-deps 的 Qt6 目录（其下有 lib/cmake/Qt6）"
)
if(MINIREC_QT_MAJOR EQUAL 6)
  if(NOT Qt6_DIR AND EXISTS "${MINIREC_OBS_DEPS_QT6}/lib/cmake/Qt6")
    list(APPEND CMAKE_PREFIX_PATH "${MINIREC_OBS_DEPS_QT6}")
  endif()
  find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)
  set(MINIREC_QT_LIBS Qt6::Widgets)
  message(STATUS "minirec: 使用 Qt6（Qt6_DIR=${Qt6_DIR}）")
else()
  if(NOT Qt5_DIR AND EXISTS "E:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5")
    set(Qt5_DIR "E:/Qt/5.15.2/msvc2019_64/lib/cmake/Qt5" CACHE PATH "Qt5 的 CMake 配置目录")
  endif()
  find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Gui Core)
  set(MINIREC_QT_LIBS Qt5::Widgets)
  message(STATUS "minirec: 使用 Qt5（${Qt5_DIR}）")
endif()

if(libobs_FOUND)
  set(MINIREC_LIBOBS OBS::libobs)
  message(STATUS "minirec: 使用安装好的 libobs 包")
else()
  # 回退：直接链接 OBS 构建产物（需要 -DOBS_SOURCE_DIR=... -DOBS_BUILD_DIR=...）
  if(NOT OBS_SOURCE_DIR OR NOT OBS_BUILD_DIR)
    message(
      FATAL_ERROR
        "未找到 libobs 包。请把 CMAKE_PREFIX_PATH 指向 OBS 安装前缀，"
        "或同时传入 -DOBS_SOURCE_DIR=<obs 源码> -DOBS_BUILD_DIR=<obs 构建目录>。"
    )
  endif()
  set(MINIREC_LIBOBS "${OBS_BUILD_DIR}/libobs/$<CONFIG>/obs.lib")
  message(STATUS "minirec: 直接链接 OBS 构建产物 ${MINIREC_LIBOBS}")
endif()

set(MINIREC_SOURCES
  src/main.cpp

  src/app/AppConfig.cpp
  src/app/SingleInstance.cpp
  src/app/TrayController.cpp
  src/app/HotkeyManager.cpp

  src/core/Logging.cpp
  src/core/LibobsRuntime.cpp
  src/core/EnumHelpers.cpp
  src/core/SceneBuilder.cpp
  src/core/EncoderFactory.cpp
  src/core/Remux.cpp
  src/core/OutputController.cpp
  src/core/RecorderController.cpp
  src/core/MouseEventHook.cpp
  src/core/MouseFxSource.cpp
  src/core/RegionSelector.cpp

  src/ui/Theme.cpp
  src/ui/MainWindow.cpp
  src/ui/SettingsDialog.cpp
  src/ui/RecordingOverlay.cpp

  resources/app.rc
)

set(MINIREC_HEADERS
  src/app/AppConfig.h
  src/app/SingleInstance.h
  src/app/TrayController.h
  src/app/HotkeyManager.h

  src/core/Logging.h
  src/core/LibobsRuntime.h
  src/core/EnumHelpers.h
  src/core/SceneBuilder.h
  src/core/EncoderFactory.h
  src/core/Remux.h
  src/core/OutputController.h
  src/core/RecorderController.h
  src/core/MouseEventHook.h
  src/core/MouseFxSource.h
  src/core/RegionSelector.h

  src/ui/Theme.h
  src/ui/MainWindow.h
  src/ui/SettingsDialog.h
  src/ui/RecordingOverlay.h
)

add_executable(minirec WIN32 ${MINIREC_SOURCES} ${MINIREC_HEADERS})

target_link_libraries(minirec PRIVATE ${MINIREC_LIBOBS} ${MINIREC_QT_LIBS})

# 回退路径下需要手动补 libobs 的头文件目录
if(NOT libobs_FOUND)
  target_include_directories(minirec PRIVATE "${OBS_SOURCE_DIR}/libobs" "${OBS_BUILD_DIR}/config")
endif()

target_include_directories(minirec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(minirec PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
  MINIREC_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
  target_compile_options(minirec PRIVATE /W3 /MP /utf-8 /permissive-)
  # 与 libobs 一样使用静态 CRT 之外的默认设置：保持 /MD
  set_property(TARGET minirec PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  # 应用清单由 resources/app.rc 内嵌（PerMonitorV2 DPI），
  # 关掉链接器自动生成的那份，否则 CVTRES 会报 CVT1100 资源重复
  target_link_options(minirec PRIVATE "/MANIFEST:NO")
endif()

# 运行时输出目录 = 构建目录下的 rundir（打包脚本从这个目录取产物）
set_target_properties(minirec PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rundir"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/rundir"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/rundir"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/rundir"
)
