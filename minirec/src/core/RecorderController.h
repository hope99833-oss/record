#pragma once

#include <QObject>
#include <QString>

#include <memory>
#include <thread>
#include <string>

#include "app/AppConfig.h"

class QWidget;

namespace minirec {

class SceneBuilder;
class OutputController;

enum class RecState {
	Idle,      // 空闲
	Countdown, // 倒计时中
	Starting,  // 正在启动输出
	Recording, // 录制中
	Paused,    // 已暂停
	Stopping,  // 正在停止（等待封装子进程收尾）
	Remuxing,  // 正在转封装 MP4
};

const char *stateText(RecState state);

// 录制编排层：UI 只与本类交互，内部依次驱动
// LibobsRuntime / SceneBuilder / EncoderFactory / OutputController / Remux
class RecorderController : public QObject {
	Q_OBJECT
public:
	explicit RecorderController(QObject *parent = nullptr);
	~RecorderController() override;

	// 启动时初始化 libobs；失败时 error 为可直接展示的原因
	bool initialize(const AppConfig &config, std::string *error);

	AppConfig config() const { return m_config; }
	void setConfig(const AppConfig &config);
	void saveConfig() const;

	RecState state() const { return m_state; }
	bool isRecording() const { return m_state == RecState::Recording || m_state == RecState::Paused; }
	bool isBusy() const { return m_state != RecState::Idle; }

	const QString &lastRecordingPath() const { return m_lastMkvPath; }
	const QString &lastMp4Path() const { return m_lastMp4Path; }
	const QString &currentEncoder() const { return m_currentEncoder; }
	const QString &outputDirectory() const { return m_outputDir; }

	// 上次是否异常退出（用于提示用户检查残留文件）
	bool hadUncleanShutdown() const { return m_hadUncleanShutdown; }

	// 区域选择（UI 的"选择区域"按钮调用）。parent 会在选择期间被临时隐藏。
	bool pickRegion(QWidget *parent, RegionRect *out);

	static QString configFilePath();
	static QString appDataDirectory();
	static QString logDirectory();

public slots:
	void startRecording();
	void stopRecording();
	void toggleRecording();
	void togglePause();
	void cancelCountdown();

signals:
	void stateChanged(minirec::RecState state);
	void countdownTick(int secondsLeft);
	void statsUpdated(qint64 elapsedMs, double droppedPercent, qulonglong bytesWritten);
	void errorOccurred(const QString &message);
	void recordingFinished(const QString &mkvPath, const QString &mp4Path);
	void encoderChanged(const QString &encoderId, const QString &description);

private slots:
	void onStatsTick();
	void onCountdownTick();
	void onOutputStopped(const QString &recordingPath);

private:
	void setState(RecState state);
	void teardownSession();
	bool beginSession(std::string *error);
	bool hasEnoughDiskSpace(const std::string &directory, std::string *error) const;
	void markSessionStart();
	void clearSessionMark();
	bool consumeSessionMark();

	AppConfig m_config;
	RecState m_state = RecState::Idle;
	bool m_initialized = false;
	bool m_hadUncleanShutdown = false;

	std::unique_ptr<SceneBuilder> m_sceneBuilder;
	std::unique_ptr<OutputController> m_outputController;

	QString m_lastMkvPath;
	QString m_lastMp4Path;
	QString m_currentEncoder;
	QString m_outputDir;

	qint64 m_startedAtMs = 0;
	qint64 m_pausedTotalMs = 0;
	qint64 m_pauseStartedAtMs = 0;
	bool m_remuxInProgress = false;
	std::thread m_remuxThread;

	class QTimer *m_statsTimer = nullptr;
	class QTimer *m_countdownTimer = nullptr;
	int m_countdownLeft = 0;
};

} // namespace minirec
