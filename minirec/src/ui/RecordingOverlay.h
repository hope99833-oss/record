#pragma once

#include <QPoint>
#include <QWidget>

class QLabel;
class QMouseEvent;
class QPushButton;
class QScreen;
class QTimer;

namespace minirec {

// 录制中悬浮控制条：无边框置顶小窗，显示计时并提供暂停/停止。
// 自身不驱动录制，只发出请求信号。
class RecordingOverlay : public QWidget {
	Q_OBJECT
public:
	explicit RecordingOverlay(QWidget *parent = nullptr);

	// 显示到指定屏幕可用区域右上角（内缩 24px）并开始计时；screen 为空时用主屏幕
	void startOnScreen(QScreen *screen);

	// 隐藏并停止计时
	void stop();

	// 暂停时计时冻结、暂停按钮文案变「继续」
	void setPaused(bool paused);

	// 例如「正在生成 MP4…」；非空时禁用按钮，空字符串恢复正常
	void setStatusText(const QString &text);

	// 由调用方提供权威时长（如 RecorderController::statsUpdated）；
	// 一旦使用过，计时改由外部驱动，内部不再自增
	void setElapsedMs(qint64 ms);

signals:
	void pauseRequested();
	void stopRequested();

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	void onTick();
	void onBlink();
	void refreshTimeText();
	void applyDotStyle();

	QLabel *m_dot = nullptr;
	QLabel *m_timeLabel = nullptr;
	QLabel *m_statusLabel = nullptr;
	QPushButton *m_pauseButton = nullptr;
	QPushButton *m_stopButton = nullptr;
	QTimer *m_timer = nullptr;
	QTimer *m_blinkTimer = nullptr;

	QPoint m_dragOffset;
	bool m_dragging = false;
	bool m_paused = false;
	bool m_statusMode = false;
	bool m_blinkingOn = true;
	bool m_internalClock = true;
	qint64 m_elapsedMs = 0;
};

} // namespace minirec
