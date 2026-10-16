#pragma once

#include <QHash>
#include <QtGlobal>
#include <atomic>
#include <thread>
#include <windows.h>
#include <evntrace.h>

namespace NetQuery {

	// 启动 / 停止 ETW 消费者。成功返回 true。
	// 非管理员、会话已存在、或 Windows 版本不支持时会返回 false。
	bool start();
	void stop();

	// 取出自上次调用以来累积的每进程字节数，并清零内部累积器。
	// 返回 true 表示数据有效。
	bool take(QHash<quint32, quint64>& outBytes);

	bool isRunning();

}