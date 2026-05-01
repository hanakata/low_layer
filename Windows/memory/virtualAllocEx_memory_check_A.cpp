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

void PrintMemoryUsage() {
    PROCESS_MEMORY_COUNTERS_EX2 pmc;
    
    // 現在のプロセスのハンドルを取得し、メモリ情報を取得
    // GetCurrentProcess() は擬似ハンドルを返すため、CloseHandle は不要
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        
        // 1. WorkingSetSize: 物理メモリ(RAM)上に存在しているメモリ量
        // 2. PrivateUsage: 他のプロセスと共有できない、そのプロセス専用の割り当て量 (コミット量)
        DbgLog("--- Current Process Memory Info ---\n");
        DbgLog("[Process A]Working Set Size:  %zu KB\n", pmc.WorkingSetSize / 1024);
        DbgLog("[Process A]Private Usage:     %zu KB\n", pmc.PrivateUsage / 1024);
        DbgLog("[Process A]Private Working Set:    %zu KB\n", pmc.PrivateWorkingSetSize / 1024);
        DbgLog("[Process A]Pagefile Usage:    %zu KB\n", pmc.PagefileUsage / 1024);
    } else {
        DbgLog("Failed to retrieve memory info. Error code: %d", GetLastError());
    }
}

int main() {
    const size_t allocationSize = 4096 * 100;
    // 1. プロセスA自身のメモリ確保（Private Bytes増加）
    LPVOID pLocalMem = VirtualAlloc(NULL, allocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pLocalMem) return 1;
    DbgLog("[Process A]Process ID: %d\n", GetCurrentProcessId());
    DbgLog("Process A Allocated Address:  %d\n", pLocalMem);
    DebugBreak();

    // 2. プロセスBの起動コマンドラインを構築
    // フォーマット: "ProcessB.exe <PID> <Address>"
    std::wstring cmdLine = L"virtualAllocEx_memory_check_B.exe " 
        + std::to_wstring(GetCurrentProcessId()) + L" " 
        + std::to_wstring(reinterpret_cast<ULONG_PTR>(pLocalMem));

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    DbgLog("[Process A]Spawning Process B...\n");
    // 3. プロセスBを子プロセスとしてキック
    BOOL success = CreateProcessW(
        L"virtualAllocEx_memory_check_B.exe",
        &cmdLine[0],  // コマンドライン引数
        NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );

    if (!success) {
        DbgLog("[Process A]CreateProcess failed. Error: %d\n", GetLastError());
        VirtualFree(pLocalMem, 0, MEM_RELEASE);
        return 1;
    }

    // 子プロセスが終了するまで待機
    WaitForSingleObject(pi.hProcess, INFINITE);
    DbgLog("[Process A]After Process B...\n");

    // ハンドルとメモリのクリーンアップ
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    VirtualFree(pLocalMem, 0, MEM_RELEASE);

    return 0;
}