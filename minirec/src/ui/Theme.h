#pragma once

#include <QIcon>
#include <QString>

class QApplication;

namespace minirec {
namespace theme {

// 深色主题 + 圆角控件，观感对齐主流录屏软件
void apply(QApplication &app);

// 录制红色（录制中状态、强调按钮使用）
QString recordColor();

// 程序图标 / 托盘图标（图标在代码里绘制，避免引入外部素材与授权问题）
QIcon appIcon();
QIcon trayIcon(bool recording);

} // namespace theme
} // namespace minirec
