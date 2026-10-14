#include "ui/Theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace minirec {
namespace theme {
namespace {

const char *kRecordColor = "#E8463A";
const char *kBrandColor = "#4B3FE3";

QPixmap drawCameraIcon(int size, bool recording)
{
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const qreal s = size;
	const QColor body = recording ? QColor(kRecordColor) : QColor(246, 246, 247);
	const QColor lens = recording ? QColor(255, 255, 255) : QColor(40, 40, 44);

	// 机身
	QRectF bodyRect(s * 0.06, s * 0.24, s * 0.72, s * 0.50);
	QPainterPath bodyPath;
	bodyPath.addRoundedRect(bodyRect, s * 0.12, s * 0.12);
	painter.fillPath(bodyPath, body);

	// 镜头
	painter.setBrush(lens);
	painter.setPen(Qt::NoPen);
	painter.drawEllipse(QPointF(bodyRect.center().x(), bodyRect.center().y()), s * 0.15, s * 0.15);

	// 右侧取景框
	QPainterPath lensPath;
	lensPath.moveTo(s * 0.80, s * 0.36);
	lensPath.lineTo(s * 0.94, s * 0.26);
	lensPath.lineTo(s * 0.94, s * 0.72);
	lensPath.lineTo(s * 0.80, s * 0.62);
	lensPath.closeSubpath();
	painter.fillPath(lensPath, body);

	painter.end();
	return pixmap;
}

} // namespace

QString recordColor()
{
	return QString::fromLatin1(kRecordColor);
}

void apply(QApplication &app)
{
	app.setStyle(QStyleFactory::create("Fusion"));

	QPalette palette;
	palette.setColor(QPalette::Window, QColor(32, 33, 36));
	palette.setColor(QPalette::WindowText, QColor(232, 234, 237));
	palette.setColor(QPalette::Base, QColor(24, 25, 28));
	palette.setColor(QPalette::AlternateBase, QColor(38, 39, 43));
	palette.setColor(QPalette::ToolTipBase, QColor(24, 25, 28));
	palette.setColor(QPalette::ToolTipText, QColor(232, 234, 237));
	palette.setColor(QPalette::Text, QColor(232, 234, 237));
	palette.setColor(QPalette::Button, QColor(45, 46, 51));
	palette.setColor(QPalette::ButtonText, QColor(232, 234, 237));
	palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
	palette.setColor(QPalette::Link, QColor(kBrandColor));
	palette.setColor(QPalette::Highlight, QColor(kBrandColor));
	palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
	palette.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 132, 138));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(130, 132, 138));
	app.setPalette(palette);

	app.setStyleSheet(R"(
		QWidget { font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif; font-size: 13px; }
		QMainWindow, QDialog { background: #202124; }
		QGroupBox {
			border: 1px solid #3A3B41; border-radius: 10px;
			margin-top: 16px; padding: 12px 12px 8px 12px;
		}
		QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #9AA0A6; }
		QComboBox, QLineEdit, QSpinBox {
			background: #2A2B30; border: 1px solid #3A3B41; border-radius: 8px;
			padding: 6px 10px; min-height: 20px;
		}
		QComboBox:hover, QLineEdit:hover, QSpinBox:hover { border-color: #4A4B52; }
		QComboBox:focus, QLineEdit:focus, QSpinBox:focus { border-color: #4B3FE3; }
		QComboBox::drop-down { border: none; width: 22px; }
		QComboBox QAbstractItemView {
			background: #2A2B30; border: 1px solid #3A3B41; border-radius: 8px;
			selection-background-color: #4B3FE3; outline: none;
		}
		QPushButton {
			background: #2F3036; border: 1px solid #3A3B41; border-radius: 8px;
			padding: 7px 14px; min-height: 20px;
		}
		QPushButton:hover { background: #35363D; }
		QPushButton:pressed { background: #26272B; }
		QPushButton:disabled { color: #7A7C82; background: #2A2B30; }
		QPushButton#recordButton {
			background: #E8463A; border: none; border-radius: 10px;
			color: #FFFFFF; font-size: 15px; font-weight: 600; padding: 12px 18px;
		}
		QPushButton#recordButton:hover { background: #F0523F; }
		QPushButton#recordButton[recording="true"] { background: #3A3B41; color: #E8EAED; }
		QCheckBox { spacing: 8px; }
		QCheckBox::indicator {
			width: 16px; height: 16px; border-radius: 4px;
			border: 1px solid #4A4B52; background: #2A2B30;
		}
		QCheckBox::indicator:checked { background: #4B3FE3; border-color: #4B3FE3; }
		QLabel#hint { color: #9AA0A6; }
		QLabel#statusLabel { color: #9AA0A6; }
		QStatusBar { background: #1B1C1F; color: #9AA0A6; }
		QMenu { background: #2A2B30; border: 1px solid #3A3B41; border-radius: 8px; padding: 4px; }
		QMenu::item { padding: 6px 22px 6px 12px; border-radius: 6px; }
		QMenu::item:selected { background: #4B3FE3; color: #FFFFFF; }
		QMenu::separator { height: 1px; background: #3A3B41; margin: 4px 8px; }
		QSlider::groove:horizontal { height: 4px; background: #3A3B41; border-radius: 2px; }
		QSlider::handle:horizontal {
			width: 14px; margin: -6px 0; border-radius: 7px; background: #4B3FE3;
		}
		QToolTip { background: #2A2B30; color: #E8EAED; border: 1px solid #3A3B41; padding: 4px 6px; }
	)");
}

QIcon appIcon()
{
	QIcon icon;
	for (int size : {16, 24, 32, 48, 64, 128, 256})
		icon.addPixmap(drawCameraIcon(size, false));
	return icon;
}

QIcon trayIcon(bool recording)
{
	QIcon icon;
	for (int size : {16, 20, 24, 32, 48, 64})
		icon.addPixmap(drawCameraIcon(size, recording));
	if (recording)
		icon.setIsMask(false);
	return icon;
}

} // namespace theme
} // namespace minirec
