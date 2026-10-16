#include "ProcessQuery.h"
#include <windows.h>
#include <winternl.h>
#include <QHash>
#include <QFileInfo>

using pNtQSI = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
static const NTSTATUS STATUS_INFO_LEN_MISMATCH = (NTSTATUS)0xC0000004L;

#pragma pack(push, 8)
struct SPI {
	ULONG NextEntryOffset;
	ULONG NumberOfThreads;
	LARGE_INTEGER WorkingSetPrivateSize;
	ULONG HardFaultCount;
	ULONG NumberOfThreadsHighWatermark;
	ULONGLONG CycleTime;
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;
	UNICODE_STRING ImageName;
	KPRIORITY BasePriority;
	HANDLE UniqueProcessId;
	HANDLE InheritedFromUniqueProcessId;
	ULONG HandleCount;
	ULONG SessionId;
	ULONG_PTR UniqueProcessKey;
	SIZE_T PeakVirtualSize;
	SIZE_T VirtualSize;
	ULONG PageFaultCount;
	SIZE_T PeakWorkingSetSize;
	SIZE_T WorkingSetSize;
	SIZE_T QuotaPeakPagedPoolUsage;
	SIZE_T QuotaPagedPoolUsage;
	SIZE_T QuotaPeakNonPagedPoolUsage;
	SIZE_T QuotaNonPagedPoolUsage;
	SIZE_T PagefileUsage;
	SIZE_T PeakPagefileUsage;
	SIZE_T PrivatePageCount;
	LARGE_INTEGER ReadOperationCount;
	LARGE_INTEGER WriteOperationCount;
	LARGE_INTEGER OtherOperationCount;
	LARGE_INTEGER ReadTransferCount;
	LARGE_INTEGER WriteTransferCount;
	LARGE_INTEGER OtherTransferCount;
};
#pragma pack(pop)

bool ProcessQuery::enumAll(QVector<RawProc>& out) {
	static pNtQSI NtQSI = nullptr;
	if (!NtQSI) {
		HMODULE h = GetModuleHandleW(L"ntdll.dll");
		NtQSI = (pNtQSI)GetProcAddress(h, "NtQuerySystemInformation");
		if (!NtQSI) return false;
	}

	ULONG size = 1 << 20;
	QByteArray buf;
	buf.resize((int)size);
	NTSTATUS st;
	for (int tries = 0; tries < 8; ++tries) {
		st = NtQSI(SystemProcessInformation, buf.data(), size, &size);
		if (st == STATUS_INFO_LEN_MISMATCH) { buf.resize((int)size); continue; }
		break;
	}
	if (st != 0) return false;

	out.clear();
	out.reserve(256);

	BYTE* p = (BYTE*)buf.data();
	for (;;) {
		SPI* spi = (SPI*)p;
		RawProc rp;
		rp.pid = (quint32)(quintptr)spi->UniqueProcessId;
		rp.createTime = (quint64)spi->CreateTime.QuadPart;
		rp.cpuTime = (quint64)(spi->UserTime.QuadPart + spi->KernelTime.QuadPart);
		rp.workingSet = (quint64)spi->WorkingSetSize;
		rp.ioBytes = (quint64)(spi->ReadTransferCount.QuadPart + spi->WriteTransferCount.QuadPart);
		if (spi->ImageName.Buffer && spi->ImageName.Length > 0)
			rp.name = QString::fromWCharArray(spi->ImageName.Buffer, spi->ImageName.Length / 2);
		out.push_back(rp);
		if (spi->NextEntryOffset == 0) break;
		p += spi->NextEntryOffset;
	}
	return true;
}

static QHash<quint32, QPair<quint64, QString>> g_pathCache;

QString ProcessQuery::resolvePath(quint32 pid, quint64 createTime) {
	auto it = g_pathCache.constFind(pid);
	if (it != g_pathCache.constEnd() && it->first == createTime)
		return it->second;

	QString path;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (h) {
		WCHAR buf[MAX_PATH * 2];
		DWORD sz = MAX_PATH * 2;
		if (QueryFullProcessImageNameW(h, 0, buf, &sz))
			path = QString::fromWCharArray(buf, sz);
		CloseHandle(h);
	}
	if (g_pathCache.size() > 4096) g_pathCache.clear();
	g_pathCache.insert(pid, { createTime, path });
	return path;
}

void ProcessQuery::clearPathCache() { g_pathCache.clear(); }