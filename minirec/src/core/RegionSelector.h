#pragma once

#include <QWidget>

#include "app/AppConfig.h"

class QScreen;

namespace minirec {

// 全屏半透明区域选择框：覆盖光标所在显示器，拖拽选区，Esc 取消。
// 返回的坐标是屏幕物理像素（与 Windows 显示器坐标系一致），可直接用于采集源的裁剪设置。
class RegionSelector : public QWidget {
	Q_OBJECT
public:
	static bool select(QWidget *parent, RegionRect *out);

signals:
	void finished();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private:
	explicit RegionSelector(QScreen *screen);

	// Qt 逻辑坐标 -> 屏幕物理像素
	QPoint toPhysical(const QPoint &local) const;

	QScreen *m_screen = nullptr;
	QPoint m_originPhysical; // 所在显示器左上角的物理像素坐标
	QPoint m_start;
	QPoint m_end;
	bool m_selecting = false;
	bool m_confirmed = false;
	qreal m_dpr = 1.0;
};

} // namespace minirec
