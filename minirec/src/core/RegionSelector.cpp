#include "core/RegionSelector.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <windows.h>

#include "core/Logging.h"

namespace minirec {
namespace {

// 拖出来的选区小于这个尺寸就当作误点（继续等用户重新画）
constexpr int kMinDrawSize = 8;

struct MonitorLookup {
	const QString *name;
	QRect rect;
	bool found;
};

BOOL CALLBACK monitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
	auto *ctx = reinterpret_cast<MonitorLookup *>(data);

	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info))
		return TRUE;

	const QString name = QString::fromWCharArray(info.szDevice);
	if (name.compare(*ctx->name, Qt::CaseInsensitive) != 0)
		return TRUE;

	ctx->rect = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
			  info.rcMonitor.bottom - info.rcMonitor.top);
	ctx->found = true;
	return FALSE;
}

// 通过设备名（\\.\DISPLAY1）把 QScreen 对应到 Windows 显示器的物理矩形
bool physicalMonitorRect(const QString &deviceName, QRect *out)
{
	MonitorLookup ctx{&deviceName, QRect(), false};
	EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&ctx));
	if (!ctx.found)
		return false;
	*out = ctx.rect;
	return true;
}

} // namespace

RegionSelector::RegionSelector(QScreen *screen) : QWidget(nullptr), m_screen(screen)
{
	// 必须让顶层窗口自带 alpha 通道：否则遮罩画在一个不透明的窗口上，
	// 看起来就是"整屏被涂黑"，而不是把桌面压暗。
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_NoSystemBackground);
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
	setCursor(Qt::CrossCursor);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);

	m_dpr = screen ? screen->devicePixelRatio() : 1.0;

	if (screen)
		setGeometry(screen->geometry()); // Qt 负责多屏虚拟桌面中的定位
	else
		setGeometry(QApplication::primaryScreen()->geometry());

	QRect physical;
	if (screen && physicalMonitorRect(screen->name(), &physical)) {
		m_originPhysical = physical.topLeft();
	} else if (screen) {
		const QRect qtGeo = screen->geometry();
		m_originPhysical = QPoint(qRound(qtGeo.x() * m_dpr), qRound(qtGeo.y() * m_dpr));
	}
}

QPoint RegionSelector::toPhysical(const QPoint &local) const
{
	return m_originPhysical + QPoint(qRound(local.x() * m_dpr), qRound(local.y() * m_dpr));
}

bool RegionSelector::select(QWidget *parent, RegionRect *out)
{
	// 遮罩是全屏置顶的半透明窗口，调用方自己的窗口不先藏起来的话，既会挡住
	// 用户想框的画面，也容易被一起框进选区。先隐藏，并等它真正从桌面上消失
	// （DWM 淡出）之后再显示遮罩，否则第一帧里还能看到主窗口。
	const bool restoreParent = parent && parent->isVisible();
	auto restoreWindow = [&]() {
		if (!restoreParent)
			return;
		parent->show();
		parent->raise();
		parent->activateWindow();
	};

	if (restoreParent) {
		parent->hide();

		QEventLoop waitHidden;
		QTimer::singleShot(220, &waitHidden, &QEventLoop::quit);
		waitHidden.exec();
	}

	QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
	if (!screen)
		screen = QGuiApplication::primaryScreen();

	RegionSelector selector(screen);
	selector.m_start = selector.mapFromGlobal(QCursor::pos());
	selector.m_end = selector.m_start;

	QEventLoop loop;
	QObject::connect(&selector, &RegionSelector::finished, &loop, &QEventLoop::quit);
	selector.show();
	selector.raise();
	selector.activateWindow();
	selector.setFocus();
	loop.exec();

	// 遮罩这时已经关掉了，无论结果如何都把调用方窗口还回来
	restoreWindow();

	if (!selector.m_confirmed)
		return false;

	const QPoint topLeft = selector.toPhysical(selector.m_start);
	const QPoint bottomRight = selector.toPhysical(selector.m_end);
	const int x = qMin(topLeft.x(), bottomRight.x());
	const int y = qMin(topLeft.y(), bottomRight.y());
	const int w = qAbs(bottomRight.x() - topLeft.x());
	const int h = qAbs(bottomRight.y() - topLeft.y());

	if (w < 16 || h < 16) {
		logWarn("区域选择：选区过小（%dx%d），已忽略", w, h);
		return false;
	}

	out->x = x;
	out->y = y;
	out->w = w;
	out->h = h;
	out->valid = true;
	logInfo("区域选择：物理像素 origin=(%d,%d) size=%dx%d", x, y, w, h);
	return true;
}

void RegionSelector::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	// 只画用户拖出来的框。不做任何窗口吸附：鼠标划过就自动改选区会让手绘很难控制。
	const QRect candidate = QRect(m_start, m_end).normalized();

	const QRect full = rect();
	const QColor veil(0, 0, 0, 130);
	const QRect sel = candidate.intersected(full);

	if (sel.isNull() || sel.width() <= 1 || sel.height() <= 1) {
		painter.fillRect(full, veil);
	} else {
		// 只压暗候选区域之外的四块，候选区域本身保持完全透明（透出真实桌面）。
		// 比"整屏填充 + CompositionMode_Clear 擦出选区"更可靠：
		// 后者依赖后端的清屏语义，一旦窗口没有 alpha 就会变成一块纯黑。
		const int topHeight = sel.top() - full.top();
		const int bottomHeight = full.bottom() - sel.bottom();
		const int leftWidth = sel.left() - full.left();
		const int rightWidth = full.right() - sel.right();

		if (topHeight > 0)
			painter.fillRect(QRect(full.left(), full.top(), full.width(), topHeight), veil);
		if (bottomHeight > 0)
			painter.fillRect(QRect(full.left(), sel.bottom() + 1, full.width(), bottomHeight), veil);
		if (leftWidth > 0)
			painter.fillRect(QRect(full.left(), sel.top(), leftWidth, sel.height()), veil);
		if (rightWidth > 0)
			painter.fillRect(QRect(sel.right() + 1, sel.top(), rightWidth, sel.height()), veil);

		QPen pen(QColor(75, 63, 227));
		pen.setWidth(2);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(sel.adjusted(0, 0, -1, -1));

		QString label = QStringLiteral("%1 × %2").arg(qRound(sel.width() * m_dpr))
					.arg(qRound(sel.height() * m_dpr));

		QFont font = painter.font();
		font.setPixelSize(13);
		painter.setFont(font);
		QSize textSize = painter.fontMetrics().boundingRect(label).size();
		QPoint boxTopLeft = sel.topLeft() + QPoint(0, -textSize.height() - 10);
		if (boxTopLeft.y() < 0)
			boxTopLeft = sel.topLeft() + QPoint(0, 8);
		const QRect box(boxTopLeft, textSize + QSize(16, 8));

		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, 180));
		painter.drawRoundedRect(box, 6, 6);
		painter.setPen(QColor(255, 255, 255));
		painter.drawText(box, Qt::AlignCenter, label);
	}

	QFont font = painter.font();
	font.setPixelSize(14);
	painter.setFont(font);
	painter.setPen(QColor(235, 236, 240));
	painter.drawText(rect().adjusted(0, 24, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
			 QStringLiteral("拖拽选择录制区域　·　Esc 取消"));
}

void RegionSelector::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	m_start = event->pos();
	m_end = event->pos();
	m_selecting = true;
	update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event)
{
	if (!m_selecting)
		return;
	m_end = event->pos();
	update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !m_selecting)
		return;

	m_end = event->pos();
	m_selecting = false;

	const QRect selection = QRect(m_start, m_end).normalized();
	if (selection.width() < kMinDrawSize || selection.height() < kMinDrawSize) {
		update(); // 误点，继续等待
		return;
	}

	m_confirmed = true;
	close();
}

void RegionSelector::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		m_confirmed = false;
		close();
		return;
	}
	QWidget::keyPressEvent(event);
}

void RegionSelector::closeEvent(QCloseEvent *event)
{
	emit finished();
	QWidget::closeEvent(event);
}

} // namespace minirec
