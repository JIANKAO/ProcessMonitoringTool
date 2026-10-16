#include "NetQuery.h"

// 说明：
// 方案 A 需要启动一个实时 ETW 会话订阅 "Microsoft-Windows-Kernel-Network"，
// 并在独立线程用 ProcessTrace 消费事件，解析 TCP/UDP 事件的 pid 与字节数。
// 这个桩为后续实现预留了接口；当前返回 false。
//
// 主程序在没有启用网络统计时不会调用这里，所以对性能没有影响。

namespace NetQuery {

	bool start() { return false; }
	void stop() {}
	bool query(QHash<quint32, quint64>& outBps) { outBps.clear(); return false; }

}