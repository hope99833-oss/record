#include "core/RecorderController.h"

#include <obs.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include <windows.h>

#include "core/Logging.h"
#include "core/LibobsRuntime.h"
#include "core/MouseEventHook.h"
#include "core/OutputController.h"
#include "core/RegionSelector.h"
#include "core/Remux.h"
#include "core/SceneBuilder.h"

namespace minirec {
namespace {

int evenFloor(int value)
{
	const int result = value - (value % 2);
	return result > 0 ? result : 2;
}

// 输出分辨率：按采集画面的宽高比适配到质量档的尺寸盒内，且不做无意义的上采样
void computeOutputSize(int baseWidth, int baseHeight, const AppConfig &config, int *outWidth, int *outHeight)
{
	const int baseW = evenFloor(baseWidth);
	const int baseH = evenFloor(baseHeight);

	// 原始分辨率档（outputWidth<=0）：直接跟随采集分辨率，不做任何缩放（2K 屏录 2K）
	if (config.outputWidth <= 0 || config.outputHeight <= 0) {
		*outWidth = baseW;
		*outHeight = baseH;
		return;
	}

	const double aspect = static_cast<double>(baseW) / static_cast<double>(baseH);

	int width = config.outputWidth;
	int height = config.outputHeight;

	if (aspect >= static_cast<double>(width) / height) {
		*outWidth = evenFloor(width);
		*outHeight = evenFloor(static_cast<int>(std::lround(width / aspect)));
	} else {
		*outHeight = evenFloor(height);
		*outWidth = evenFloor(static_cast<int>(std::lround(height * aspect)));
	}

	// 不超过采集分辨率（避免把 720p 屏幕放大到 1080p 白白浪费码率）
	if (*outWidth > baseW || *outHeight > baseH) {
		*outWidth = baseW;
		*outHeight = baseH;
	}

	if (*outWidth < 320 || *outHeight < 240) {
		*outWidth = baseW;
		*outHeight = baseH;
	}
}

std::string sessionMarkPath()
{
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	return QDir(dir).filePath("recording.lock").toStdString();
}

} // namespace

const char *stateText(RecState state)
{
	switch (state) {
	case RecState::Idle:
		return "就绪";
	case RecState::Countdown:
		return "倒计时";
	case RecState::Starting:
		return "正在启动";
	case RecState::Recording:
		return "录制中";
	case RecState::Paused:
		return "已暂停";
	case RecState::Stopping:
		return "正在停止";
	case RecState::Remuxing:
		return "正在生成 MP4";
	default:
		return "未知";
	}
}

RecorderController::RecorderController(QObject *parent) : QObject(parent)
{
	qRegisterMetaType<RecState>("minirec::RecState");
}

RecorderController::~RecorderController()
{
	if (m_remuxThread.joinable())
		m_remuxThread.join();

	MouseEventHook::instance().stop();
	teardownSession();
	LibobsRuntime::instance().shutdown();
	shutdownLogging();
}

QString RecorderController::appDataDirectory()
{
	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (dir.isEmpty())
		dir = QDir::homePath() + "/.minirec";
	QDir().mkpath(dir);
	return dir;
}

QString RecorderController::logDirectory()
{
	QString dir = appDataDirectory() + "/logs";
	QDir().mkpath(dir);
	return dir;
}

QString RecorderController::configFilePath()
{
	return appDataDirectory() + "/config.json";
}

bool RecorderController::initialize(const AppConfig &config, std::string *error)
{
	m_config = config;
	m_outputDir = QString::fromStdString(m_config.outputDir);
	if (m_outputDir.isEmpty())
		m_outputDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/MiniRec";
	QDir().mkpath(m_outputDir);

	initLogging(logDirectory().toStdString());
	logInfo("MiniRec 配置目录：%s", appDataDirectory().toStdString().c_str());

	m_hadUncleanShutdown = consumeSessionMark();
	if (m_hadUncleanShutdown)
		logWarn("检测到上次录制未正常收尾，录制目录里可能存在需要用播放器验证的 MKV 文件");

	if (!LibobsRuntime::instance().init(error))
		return false;

	m_sceneBuilder = std::make_unique<SceneBuilder>();
	m_outputController = std::make_unique<OutputController>();
	m_outputController->setStopCallback([this](const std::string &path) {
		const QString qpath = QString::fromStdString(path);
		QMetaObject::invokeMethod(this, "onOutputStopped", Qt::QueuedConnection, Q_ARG(QString, qpath));
	});

	m_statsTimer = new QTimer(this);
	m_statsTimer->setInterval(500);
	connect(m_statsTimer, &QTimer::timeout, this, &RecorderController::onStatsTick);

	m_countdownTimer = new QTimer(this);
	m_countdownTimer->setInterval(1000);
	connect(m_countdownTimer, &QTimer::timeout, this, &RecorderController::onCountdownTick);

	m_initialized = true;
	return true;
}

void RecorderController::setConfig(const AppConfig &config)
{
	m_config = config;
	if (!m_config.outputDir.empty())
		m_outputDir = QString::fromStdString(m_config.outputDir);
}

void RecorderController::saveConfig() const
{
	m_config.save(configFilePath().toStdString());
}

bool RecorderController::pickRegion(QWidget *parent, RegionRect *out)
{
	return RegionSelector::select(parent, out);
}

void RecorderController::setState(RecState state)
{
	if (m_state == state)
		return;
	m_state = state;
	emit stateChanged(state);
}

void RecorderController::toggleRecording()
{
	if (isRecording())
		stopRecording();
	else if (m_state == RecState::Countdown)
		cancelCountdown();
	else
		startRecording();
}

void RecorderController::startRecording()
{
	if (!m_initialized) {
		emit errorOccurred(QStringLiteral("libobs 尚未初始化完成"));
		return;
	}
	if (isBusy())
		return;

	if (m_config.countdownSeconds > 0) {
		m_countdownLeft = m_config.countdownSeconds;
		setState(RecState::Countdown);
		emit countdownTick(m_countdownLeft);
		if (m_countdownTimer)
			m_countdownTimer->start();
		return;
	}

	std::string error;
	if (!beginSession(&error)) {
		setState(RecState::Idle);
		emit errorOccurred(QString::fromStdString(error));
	}
}

void RecorderController::onCountdownTick()
{
	if (m_state != RecState::Countdown) {
		if (m_countdownTimer)
			m_countdownTimer->stop();
		return;
	}

	m_countdownLeft--;
	if (m_countdownLeft > 0) {
		emit countdownTick(m_countdownLeft);
		return;
	}

	if (m_countdownTimer)
		m_countdownTimer->stop();

	std::string error;
	if (!beginSession(&error)) {
		setState(RecState::Idle);
		emit errorOccurred(QString::fromStdString(error));
	}
}

void RecorderController::cancelCountdown()
{
	if (m_state != RecState::Countdown)
		return;
	if (m_countdownTimer)
		m_countdownTimer->stop();
	setState(RecState::Idle);
}

bool RecorderController::hasEnoughDiskSpace(const std::string &directory, std::string *error) const
{
	ULARGE_INTEGER freeBytesAvailable{};
	if (!GetDiskFreeSpaceExA(directory.c_str(), &freeBytesAvailable, nullptr, nullptr)) {
		if (error)
			*error = "无法读取磁盘剩余空间：" + directory;
		return false;
	}

	const double freeGb = static_cast<double>(freeBytesAvailable.QuadPart) / (1024.0 * 1024.0 * 1024.0);
	if (freeGb < 1.0) {
		if (error) {
			char buffer[160];
			snprintf(buffer, sizeof(buffer), "磁盘剩余空间不足（仅 %.1f GB），请清理后重试", freeGb);
			*error = buffer;
		}
		return false;
	}

	return true;
}

bool RecorderController::beginSession(std::string *error)
{
	setState(RecState::Starting);

	const std::string outputDir = m_outputDir.toStdString();
	if (!hasEnoughDiskSpace(outputDir, error)) {
		return false;
	}

	// 1) 场景与源（采集源尺寸是异步就绪的，build 内部会等）
	if (!m_sceneBuilder->build(m_config, error))
		return false;

	// 2) 画布尺寸与输出分辨率
	int canvasWidth = 0;
	int canvasHeight = 0;
	m_sceneBuilder->capturedSize(&canvasWidth, &canvasHeight);
	if (canvasWidth <= 0 || canvasHeight <= 0) {
		if (error)
			*error = "无法确定采集画面尺寸（采集源尚未出图）";
		teardownSession();
		return false;
	}
	canvasWidth = evenFloor(canvasWidth);
	canvasHeight = evenFloor(canvasHeight);

	int outputWidth = 0;
	int outputHeight = 0;
	computeOutputSize(canvasWidth, canvasHeight, m_config, &outputWidth, &outputHeight);

	if (!LibobsRuntime::instance().applyVideo(m_config.fps, outputWidth, outputHeight, canvasWidth, canvasHeight,
						  error)) {
		teardownSession();
		return false;
	}

	// 3) 按最终画布尺寸摆放各个场景项
	m_sceneBuilder->layout(canvasWidth, canvasHeight);

	// 4) 录像输出
	const QString fileName = QStringLiteral("MiniRec_%1.mkv")
					 .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
	const QString filePath = QDir(m_outputDir).filePath(fileName);

	if (!m_outputController->prepare(m_config, filePath.toStdString(), error)) {
		teardownSession();
		return false;
	}

	if (m_config.mouseFxEnabled && m_sceneBuilder->mouseFxActive())
		MouseEventHook::instance().start();

	if (!m_outputController->start(error)) {
		MouseEventHook::instance().stop();
		teardownSession();
		return false;
	}

	// 5) 记账
	m_lastMkvPath = filePath;
	m_lastMp4Path.clear();
	m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
	m_pausedTotalMs = 0;
	m_pauseStartedAtMs = 0;
	markSessionStart();

	if (m_statsTimer)
		m_statsTimer->start();

	setState(RecState::Recording);
	return true;
}

void RecorderController::stopRecording()
{
	if (m_state == RecState::Countdown) {
		cancelCountdown();
		return;
	}

	if (!isRecording())
		return;

	if (m_state == RecState::Paused) {
		// 先恢复再停止，避免 muxer 处于暂停态
		m_outputController->setPaused(false);
	}

	setState(RecState::Stopping);
	if (m_statsTimer)
		m_statsTimer->stop();

	emit statsUpdated(m_startedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch() - m_startedAtMs - m_pausedTotalMs : 0,
			  m_outputController ? m_outputController->droppedPercent() : 0.0,
			  m_outputController ? static_cast<qulonglong>(m_outputController->totalBytes()) : 0);

	m_outputController->stop(); // 异步：onOutputStopped 里收尾
}

void RecorderController::togglePause()
{
	if (!isRecording() || !m_outputController)
		return;

	const bool pauseNow = (m_state == RecState::Recording);
	m_outputController->setPaused(pauseNow);

	if (pauseNow) {
		m_pauseStartedAtMs = QDateTime::currentMSecsSinceEpoch();
		setState(RecState::Paused);
	} else {
		if (m_pauseStartedAtMs > 0)
			m_pausedTotalMs += QDateTime::currentMSecsSinceEpoch() - m_pauseStartedAtMs;
		m_pauseStartedAtMs = 0;
		setState(RecState::Recording);
	}
}

void RecorderController::onStatsTick()
{
	if (!isRecording())
		return;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	qint64 elapsed = now - m_startedAtMs - m_pausedTotalMs;
	if (m_pauseStartedAtMs > 0)
		elapsed -= (now - m_pauseStartedAtMs);
	if (elapsed < 0)
		elapsed = 0;

	const double dropped = m_outputController ? m_outputController->droppedPercent() : 0.0;
	const qulonglong bytes = m_outputController ? static_cast<qulonglong>(m_outputController->totalBytes()) : 0;

	emit statsUpdated(elapsed, dropped, bytes);
}

void RecorderController::onOutputStopped(const QString &recordingPath)
{
	logInfo("录制收尾：%s", recordingPath.toStdString().c_str());

	MouseEventHook::instance().stop();
	teardownSession();
	clearSessionMark();

	m_lastMkvPath = recordingPath;
	m_lastMp4Path.clear();

	if (!m_config.autoRemuxMp4) {
		setState(RecState::Idle);
		emit recordingFinished(recordingPath, QString());
		return;
	}

	setState(RecState::Remuxing);

	const QString mkvPath = recordingPath;
	QString mp4Path = recordingPath;
	mp4Path.replace(QStringLiteral(".mkv"), QStringLiteral(".mp4"));

	const std::string mkv = mkvPath.toStdString();
	const std::string mp4 = mp4Path.toStdString();
	const bool keepMkv = m_config.keepMkv;

	if (m_remuxThread.joinable())
		m_remuxThread.join();

	m_remuxThread = std::thread([this, mkv, mp4, keepMkv]() {
		std::string error;
		int percent = 0;
		const bool ok = Remux::remuxToMp4(mkv, mp4, &error, &percent);

		QMetaObject::invokeMethod(
			this,
			[this, ok, keepMkv, mkv, mp4, error]() {
				if (ok) {
					if (!keepMkv) {
						std::error_code ec;
						std::filesystem::remove(mkv, ec);
					}
					m_lastMp4Path = QString::fromStdString(mp4);
					m_lastMkvPath = keepMkv ? QString::fromStdString(mkv) : QString();
					logInfo("录制完成：%s", m_lastMp4Path.toStdString().c_str());
				} else {
					m_lastMkvPath = QString::fromStdString(mkv);
					emit errorOccurred(QStringLiteral("生成 MP4 失败（MKV 文件已保留）：%1")
								   .arg(QString::fromStdString(error)));
				}

				setState(RecState::Idle);
				emit recordingFinished(QString::fromStdString(mkv),
						       ok ? QString::fromStdString(mp4) : QString());
			},
			Qt::QueuedConnection);
	});
}

void RecorderController::teardownSession()
{
	if (m_statsTimer)
		m_statsTimer->stop();

	if (m_outputController)
		m_outputController->destroy();

	if (m_sceneBuilder)
		m_sceneBuilder->destroy();

	m_startedAtMs = 0;
	m_pausedTotalMs = 0;
	m_pauseStartedAtMs = 0;
}

void RecorderController::markSessionStart()
{
	QFile file(QString::fromStdString(sessionMarkPath()));
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		file.write(m_lastMkvPath.toUtf8());
}

void RecorderController::clearSessionMark()
{
	QFile::remove(QString::fromStdString(sessionMarkPath()));
}

bool RecorderController::consumeSessionMark()
{
	const QString path = QString::fromStdString(sessionMarkPath());
	if (!QFileInfo::exists(path))
		return false;

	QFile::remove(path);
	return true;
}

} // namespace minirec
