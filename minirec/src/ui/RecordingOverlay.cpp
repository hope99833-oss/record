#include "ui/RecordingOverlay.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScreen>
#include <QTimer>

#include "ui/Theme.h"

namespace minirec {
namespace {

// 距离屏幕可用区域右上角的内缩量
constexpr int kMargin = 24;
constexpr int kTickIntervalMs = 500;

} // namespace

RecordingOverlay::RecordingOverlay(QWidget *parent) : QWidget(parent)
{
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);

	m_dot = new QLabel(this);
	m_dot->setFixedSize(12, 12);

	m_timeLabel = new QLabel(QStringLiteral("00:00:00"), this);
	m_timeLabel->setObjectName(QStringLiteral("timeLabel"));

	m_statusLabel = new QLabel(this);
	m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
	m_statusLabel->hide();

	m_pauseButton = new QPushButton(QStringLiteral("暂停"), this);
	m_pauseButton->setCursor(Qt::PointingHandCursor);

	m_stopButton = new QPushButton(QStringLiteral("停止"), this);
	m_stopButton->setObjectName(QStringLiteral("stopButton"));
	m_stopButton->setCursor(Qt::PointingHandCursor);

	connect(m_pauseButton, &QPushButton::clicked, this, &RecordingOverlay::pauseRequested);
	connect(m_stopButton, &QPushButton::clicked, this, &RecordingOverlay::stopRequested);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(14, 10, 14, 10);
	layout->setSpacing(10);
	layout->addWidget(m_dot);
	layout->addWidget(m_timeLabel);
	layout->addWidget(m_statusLabel);
	layout->addWidget(m_pauseButton);
	layout->addWidget(m_stopButton);

	setStyleSheet(QStringLiteral(
		"QLabel#timeLabel { color: #FFFFFF; font-family: Consolas, \"Courier New\", monospace;"
		" font-size: 15px; font-weight: 600; letter-spacing: 1px; }"
		"QLabel#statusLabel { color: #B6B9BE; }"
		"QPushButton { background: #35363D; border: 1px solid #4A4B52; border-radius: 7px;"
		" color: #E8EAED; padding: 5px 12px; min-height: 20px; }"
		"QPushButton:hover { background: #3E3F47; }"
		"QPushButton:pressed { background: #2A2B30; }"
		"QPushButton:disabled { background: #2A2B30; color: #7A7C82; }"
		"QPushButton#stopButton { background: %1; border: none; color: #FFFFFF; font-weight: 600; }"
		"QPushButton#stopButton:hover { background: #F0523F; }"
		"QPushButton#stopButton:disabled { background: #6B3532; color: #C9A9A6; }")
			      .arg(theme::recordColor()));

	m_timer = new QTimer(this);
	m_timer->setInterval(kTickIntervalMs);
	connect(m_timer, &QTimer::timeout, this, &RecordingOverlay::onTick);

	m_blinkTimer = new QTimer(this);
	m_blinkTimer->setInterval(kTickIntervalMs);
	connect(m_blinkTimer, &QTimer::timeout, this, &RecordingOverlay::onBlink);

	applyDotStyle();
}

void RecordingOverlay::startOnScreen(QScreen *screen)
{
	if (!screen)
		screen = QGuiApplication::primaryScreen();

	m_elapsedMs = 0;
	m_internalClock = true;
	m_paused = false;
	m_statusMode = false;
	m_blinkingOn = true;
	m_dragging = false;

	m_timeLabel->setText(QStringLiteral("00:00:00"));
	m_statusLabel->clear();
	m_statusLabel->hide();
	m_pauseButton->setText(QStringLiteral("暂停"));
	m_pauseButton->setEnabled(true);
	m_stopButton->setEnabled(true);
	applyDotStyle();

	adjustSize();

	if (screen) {
		const QRect available = screen->availableGeometry();
		const int x = available.x() + available.width() - width() - kMargin;
		const int y = available.y() + kMargin;
		move(qMax(available.x(), x), y);
	}

	show();
	raise();
	m_timer->start();
	m_blinkTimer->start();
}

void RecordingOverlay::stop()
{
	m_timer->stop();
	m_blinkTimer->stop();
	m_dragging = false;
	hide();
}

void RecordingOverlay::setPaused(bool paused)
{
	m_paused = paused;
	m_pauseButton->setText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
	if (!paused)
		m_blinkingOn = true;

	applyDotStyle();
}

void RecordingOverlay::setStatusText(const QString &text)
{
	m_statusMode = !text.isEmpty();
	m_statusLabel->setText(text);
	m_statusLabel->setVisible(m_statusMode);

	// 收尾阶段（如正在生成 MP4）不允许再操作
	m_pauseButton->setEnabled(!m_statusMode);
	m_stopButton->setEnabled(!m_statusMode);

	applyDotStyle();
	adjustSize();
}

void RecordingOverlay::setElapsedMs(qint64 ms)
{
	m_elapsedMs = qMax<qint64>(0, ms);
	m_internalClock = false;
	refreshTimeText();
}

void RecordingOverlay::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setBrush(QColor(20, 21, 24, 225));
	painter.setPen(QPen(QColor(255, 255, 255, 36), 1.0));
	painter.drawRoundedRect(rect, 12.0, 12.0);
}

void RecordingOverlay::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		m_dragging = true;
		m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
		event->accept();
		return;
	}

	QWidget::mousePressEvent(event);
}

void RecordingOverlay::mouseMoveEvent(QMouseEvent *event)
{
	if (m_dragging && (event->buttons() & Qt::LeftButton)) {
		move(event->globalPosition().toPoint() - m_dragOffset);
		event->accept();
		return;
	}

	QWidget::mouseMoveEvent(event);
}

void RecordingOverlay::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		m_dragging = false;
		event->accept();
		return;
	}

	QWidget::mouseReleaseEvent(event);
}

void RecordingOverlay::onTick()
{
	if (m_internalClock && !m_paused)
		m_elapsedMs += kTickIntervalMs;

	refreshTimeText();
}

void RecordingOverlay::onBlink()
{
	// 暂停或收尾阶段圆点保持静态
	if (m_paused || m_statusMode)
		return;

	m_blinkingOn = !m_blinkingOn;
	applyDotStyle();
}

void RecordingOverlay::refreshTimeText()
{
	const qint64 totalSeconds = m_elapsedMs / 1000;
	const qint64 hours = totalSeconds / 3600;
	const qint64 minutes = (totalSeconds % 3600) / 60;
	const qint64 seconds = totalSeconds % 60;

	m_timeLabel->setText(QStringLiteral("%1:%2:%3")
				     .arg(hours, 2, 10, QLatin1Char('0'))
				     .arg(minutes, 2, 10, QLatin1Char('0'))
				     .arg(seconds, 2, 10, QLatin1Char('0')));
}

void RecordingOverlay::applyDotStyle()
{
	QString color;
	if (m_statusMode)
		color = QStringLiteral("#5A5C63");
	else if (m_paused)
		color = theme::recordColor();
	else
		color = m_blinkingOn ? theme::recordColor() : QStringLiteral("#5A5C63");

	m_dot->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 6px;").arg(color));
}

} // namespace minirec
