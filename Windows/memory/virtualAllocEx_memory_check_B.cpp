#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <iomanip>
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

// 自分（B）と相手（A）のメモリ情報を並べて出力する関数
void PrintBothMemoryUsage(HANDLE hProcessA, const char* stageName) {
    PROCESS_MEMORY_COUNTERS_EX pmcA;
    PROCESS_MEMORY_COUNTERS_EX pmcB;
    
    // プロセスAとプロセスBの情報を両方取得
    BOOL okA = GetProcessMemoryInfo(hProcessA, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmcA), sizeof(pmcA));
    BOOL okB = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmcB), sizeof(pmcB));

    if (okA && okB) {
        DbgLog("===== [ Memory Comparison: %s ] =====\n", stageName);
        DbgLog("[Process A (Target)] Private: %zu KB | WorkingSet: %zu KB\n", 
               pmcA.PrivateUsage / 1024, pmcA.WorkingSetSize / 1024);
        DbgLog("[Process B (Self)  ] Private: %zu KB | WorkingSet: %zu KB\n", 
               pmcB.PrivateUsage / 1024, pmcB.WorkingSetSize / 1024);
        DbgLog("==============================================\n\n");
    } else {
        DbgLog("Failed to retrieve memory info. Error code: %d\n", GetLastError());
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        DbgLog("Usage: ProcessB.exe <TargetPID> <TargetAddress>\n");
        return 1;
    }

    DWORD pid = std::stoul(argv[1]);
    // A側が std::to_wstring で出力しているため、10進数としてパース
    ULONG_PTR addrValue = std::stoull(argv[2], nullptr, 10);
    LPVOID pRemoteMem = reinterpret_cast<LPVOID>(addrValue);

    DbgLog("Process B started. Target PID: %d, Target Addr: %p\n", pid, pRemoteMem);

    // プロセスAのハンドルを取得（メモリ情報取得のために PROCESS_QUERY_INFORMATION が必須）
    HANDLE hProcess = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE,
        pid
    );

    if (hProcess == NULL) {
        DbgLog("OpenProcess failed. Error: %d\n", GetLastError());
        return 1;
    }

    // --- 検証フェーズ 1: 初期状態 ---
    PrintBothMemoryUsage(hProcess, "1. Initial State");
    DebugBreak(); 

    // --- 検証フェーズ 2: VirtualAllocEx の実行 ---
    const size_t allocSize = 4096 * 100; // 400KB
    LPVOID pAllocatedMem = VirtualAllocEx(hProcess, NULL, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DbgLog("Process B allocated memory in Process A at: %p\n", pAllocatedMem);
    if (pAllocatedMem == NULL) {
        DbgLog("VirtualAllocEx failed. Error: %d\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }

    // ここで A の Private だけが増え、B は変わらないことを確認
    PrintBothMemoryUsage(hProcess, "2. After VirtualAllocEx");
    DebugBreak(); 

    // --- 検証フェーズ 3: WriteProcessMemory の実行 ---
    char* data = new char[allocSize];
    memset(data, 0x41, allocSize); // ダミーデータ

    SIZE_T bytesWritten = 0;
    BOOL result = WriteProcessMemory(hProcess, pAllocatedMem, data, allocSize, &bytesWritten);
    delete[] data;

    if (!result) {
        DbgLog("WriteProcessMemory failed. Error: %d\n", GetLastError());
        VirtualFreeEx(hProcess, pAllocatedMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    // ここで A の Working Set が増え、B は変わらないことを確認
    PrintBothMemoryUsage(hProcess, "3. After WriteProcessMemory");
    DebugBreak(); 

    CloseHandle(hProcess);
    DbgLog("Process B finished successfully.\n");
    return 0;
}