#include "NetQuery.h"

#include <QDebug>
#include <QString>
#include <mutex>
#include <memory>

#include <evntcons.h>
#include <evntrace.h>
#include <tdh.h>
//// 调试用：限制失败日志最多打印 N 条，避免刷屏
//static std::atomic_int g_parseFailCount{ 0 };
//static const int kParseFailLogLimit = 8;

namespace NetQuery {

	// ---- 常量 ----
	static const wchar_t* kSessionName = L"ProcessMonitoringTool-Net";
	// Microsoft-Windows-Kernel-Network 的 GUID
	static const GUID kProviderGuid =
	{ 0x7dd42a49, 0x5329, 0x4832, { 0x8d, 0xfd, 0x43, 0xd9, 0x79, 0x15, 0x3a, 0x88 } };

	// 关键字：IPv4(0x10) + IPv6(0x20)
	static const ULONGLONG kMatchAnyKeyword = 0x10 | 0x20;
	// ---- 前向声明（必须在使用之前） ----
	static void WINAPI eventRecordCallback(PEVENT_RECORD ev);
	// ---- 全局状态 ----
	static TRACEHANDLE  g_session = 0;   // 控制会话句柄
	static TRACEHANDLE  g_trace = 0;   // 消费者句柄
	static std::thread  g_thread;           // ProcessTrace 阻塞所在的线程
	static std::atomic_bool g_running{ false };
	static std::atomic_bool g_stopFlag{ false };

	// 每进程累积字节数，由回调线程写入，Sampler 线程读取
	static std::mutex        g_mutex;
	static QHash<quint32, quint64> g_accum;

} // namespace NetQuery

namespace NetQuery {

	// 确保同名会话已被清理（上此异常退出可能残留）
	static void ensureNoStaleSession() {
		// EVENT_TRACE_PROPERTIES 是变长结构，尾部要容纳 session name
		const size_t bufSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(kSessionName) + 1) * sizeof(wchar_t);
		auto buf = std::make_unique<BYTE[]>(bufSize);
		ZeroMemory(buf.get(), bufSize);

		auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.get());
		props->Wnode.BufferSize = (ULONG)bufSize;
		props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
		wcscpy_s(reinterpret_cast<wchar_t*>(buf.get() + props->LoggerNameOffset),
			wcslen(kSessionName) + 1, kSessionName);

		// 如果会话不存在，返回 ERROR_WMI_INSTANCE_NOT_FOUND，忽略即可
		ControlTraceW(0, kSessionName, props, EVENT_TRACE_CONTROL_STOP);
	}

	bool start() {
		if (g_running.load()) return true;

		ensureNoStaleSession();

		// ---- 1. 配置并启动 ETW 会话 ----
		const size_t bufSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(kSessionName) + 1) * sizeof(wchar_t);
		auto buf = std::make_unique<BYTE[]>(bufSize);
		ZeroMemory(buf.get(), bufSize);

		auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.get());
		props->Wnode.BufferSize = (ULONG)bufSize;
		props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
		props->Wnode.ClientContext = 1;               // QPC 时钟
		props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
		props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
		props->FlushTimer = 1;                 // 1 秒刷一次，降低事件延迟

		// 用随机 GUID 作为会话标识（不能用 Provider GUID）
		GUID sessionGuid = { 0 };
		CoCreateGuid(&sessionGuid);
		props->Wnode.Guid = sessionGuid;

		ULONG rc = StartTraceW(&g_session, kSessionName, props);
		if (rc != ERROR_SUCCESS) {
			qWarning() << "StartTrace failed:" << rc;
			g_session = 0;
			return false;
		}

		// ---- 2. 让该会话接收 Kernel-Network 提供程序的事件 ----
		rc = EnableTraceEx2(
			g_session,
			&kProviderGuid,
			EVENT_CONTROL_CODE_ENABLE_PROVIDER,
			TRACE_LEVEL_INFORMATION,   // Level：信息级，足够拿到 send/recv
			kMatchAnyKeyword,
			0,
			0,
			nullptr);

		if (rc != ERROR_SUCCESS) {
			qWarning() << "EnableTraceEx2 failed:" << rc;
			ensureNoStaleSession();
			g_session = 0;
			return false;
		}

		// ---- 3. 打开实时消费者 ----
		EVENT_TRACE_LOGFILEW logfile = { 0 };
		logfile.LoggerName = const_cast<LPWSTR>(kSessionName);
		logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME
			| PROCESS_TRACE_MODE_EVENT_RECORD;
		logfile.EventRecordCallback = &eventRecordCallback;  // 见下一块

		g_trace = OpenTraceW(&logfile);
		if (g_trace == INVALID_PROCESSTRACE_HANDLE) {
			qWarning() << "OpenTrace failed:" << GetLastError();
			ensureNoStaleSession();
			g_session = 0;
			g_trace = 0;
			return false;
		}

		// ---- 4. 在独立线程上跑 ProcessTrace ----
		// ProcessTrace 是阻塞的，必须放在自己的线程里
		g_stopFlag = false;
		g_running = true;
		g_thread = std::thread([] {
			// 阻塞直到 stop() 调用 ControlTrace 停止会话
			ProcessTrace(&g_trace, 1, nullptr, nullptr);
			g_running = false;
			});

		return true;
	}

	void stop() {
		if (!g_running.load() && g_session == 0) return;

		g_stopFlag = true;

		// 停止会话会让 ProcessTrace 返回
		if (g_session) {
			ensureNoStaleSession();
			g_session = 0;
		}

		// 等待消费者线程退出
		if (g_thread.joinable())
			g_thread.join();

		if (g_trace && g_trace != INVALID_PROCESSTRACE_HANDLE) {
			CloseTrace(g_trace);
			g_trace = 0;
		}
		g_running = false;
	}

	bool isRunning() { return g_running.load(); }

} // namespace NetQuery

namespace NetQuery {

	// 事件类型常量。Kernel-Network 的 send/recv 事件 ID：
	//   EventID 10 = TcpIp Send (IPv4)
	//   EventID 11 = TcpIp Recv (IPv4)
	//   EventID 26 = TcpIp Send (IPv6)
	//   EventID 27 = TcpIp Recv (IPv6)
	static bool isSendEvent(USHORT id) {
		return id == 10 || id == 26;
	}
	static bool isRecvEvent(USHORT id) {
		return id == 11 || id == 27;
	}

	// 回调：每个 ETW 事件到达时被 ProcessTrace 调用
	static void WINAPI eventRecordCallback(PEVENT_RECORD ev) {
		if (!ev || !ev->UserData || ev->UserDataLength == 0) return;

		const USHORT eventId = ev->EventHeader.EventDescriptor.Id;
		if (!isSendEvent(eventId) && !isRecvEvent(eventId)) return;

		PROPERTY_DATA_DESCRIPTOR props[2] = {
			{ (ULONGLONG)L"PID",  0, 0 },
			{ (ULONGLONG)L"size", 0, 0 },
		};

		quint32 pid = 0;
		quint32 size = 0;
		ULONG sizeOut = sizeof(quint32);

		const ULONG r1 = TdhGetProperty(ev, 0, nullptr, 1, &props[0], sizeOut, (PBYTE)&pid);
		const ULONG r2 = TdhGetProperty(ev, 0, nullptr, 1, &props[1], sizeOut, (PBYTE)&size);

		if (r1 != ERROR_SUCCESS || r2 != ERROR_SUCCESS) {
			//// 只在调试期打日志，最多打 kParseFailLogLimit 条
			//int n = g_parseFailCount.fetch_add(1);
			//if (n < kParseFailLogLimit) {
			//	qWarning("[NetQuery] TdhGetProperty failed, eventId=%u provider=%08X-%04X-%04X "
			//		"r1=%lu r2=%lu userDataLen=%u",
			//		(unsigned)eventId,
			//		ev->EventHeader.ProviderId.Data1,
			//		ev->EventHeader.ProviderId.Data2,
			//		ev->EventHeader.ProviderId.Data3,
			//		(unsigned long)r1,
			//		(unsigned long)r2,
			//		(unsigned)ev->UserDataLength);
			//}
			return;
		}

		if (pid == 0) return;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_accum[pid] += size;
		}
	}
} // namespace NetQuery

namespace NetQuery {

	bool take(QHash<quint32, quint64>& outBytes) {
		if (!g_running.load()) {
			outBytes.clear();
			return false;
		}

		std::lock_guard<std::mutex> lock(g_mutex);
		outBytes = std::move(g_accum);
		g_accum.clear();
		return true;
	}

} // namespace NetQuery