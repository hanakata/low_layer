#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <string>

#pragma comment(lib, "psapi.lib")

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

void PrintBothMemoryUsage(HANDLE hProcessA, const char* stageName) {
    PROCESS_MEMORY_COUNTERS_EX pmcA;
    PROCESS_MEMORY_COUNTERS_EX pmcB;
    
    GetProcessMemoryInfo(hProcessA, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmcA), sizeof(pmcA));
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmcB), sizeof(pmcB));

    DbgLog("===== [ Memory Comparison: %s ] =====\n", stageName);
    // 共有メモリの場合、PrivateUsageではなくWorkingSetSizeの変動に注目
    DbgLog("[Process A] Private: %zu KB | WorkingSet: %zu KB\n", pmcA.PrivateUsage / 1024, pmcA.WorkingSetSize / 1024);
    DbgLog("[Process B] Private: %zu KB | WorkingSet: %zu KB\n", pmcB.PrivateUsage / 1024, pmcB.WorkingSetSize / 1024);
    DbgLog("==============================================\n\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;

    DWORD pid = std::stoul(argv[1]);
    const size_t allocSize = 4096 * 100;
    const wchar_t* szMappingName = L"Local\\MySharedMemoryVerify";

    HANDLE hProcessA = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcessA) return 1;

    // --- 検証フェーズ 1: マッピング前 ---
    PrintBothMemoryUsage(hProcessA, "1. Before Open/Map");
    DebugBreak();

    // 1. プロセスAが作ったオブジェクトを名前でオープン
    HANDLE hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, szMappingName);
    if (hMapFile == NULL) {
        CloseHandle(hProcessA);
        return 1;
    }

    // 2. 自空間にマッピング（Aとは異なる仮想アドレスになる可能性がある）
    LPVOID pRemoteMem = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, allocSize);
    DbgLog("[Process B] Process ID: %d, Mapped Address: %p\n", GetCurrentProcessId(), pRemoteMem);
    // --- 検証フェーズ 2: マッピング直後（まだ誰も書き込んでいない） ---
    // Private/WorkingSet 共に大きな変動はないはず
    PrintBothMemoryUsage(hProcessA, "2. After MapViewOfFile");
    DebugBreak();

    // --- 検証フェーズ 3: プロセスBによる物理メモリへのコミット（書き込み） ---
    // ここでデータを書き込むと、物理ページが割り当てられる
    if (pRemoteMem) {
        memset(pRemoteMem, 0x41, allocSize);
    }

    // 【重要】ここでプロセスBだけでなく、プロセスAの「WorkingSetSize」も同時に増加することを確認してください
    // （PrivateUsageは双方とも増えません）
    PrintBothMemoryUsage(hProcessA, "3. After Process B Writes");
    DebugBreak();

    UnmapViewOfFile(pRemoteMem);
    CloseHandle(hMapFile);
    CloseHandle(hProcessA);

    return 0;
}