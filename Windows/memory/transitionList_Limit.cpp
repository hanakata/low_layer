#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <iomanip>

// リンク設定 (ビルド環境によっては psapi.lib が必要)
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
        DbgLog("Working Set Size:  %zu KB\n", pmc.WorkingSetSize / 1024);
        DbgLog("Private Usage:     %zu KB\n", pmc.PrivateUsage / 1024);
        DbgLog("Private Working Set:    %zu KB\n", pmc.PrivateWorkingSetSize / 1024);
        DbgLog("Pagefile Usage:    %zu KB\n", pmc.PagefileUsage / 1024);
    } else {
        DbgLog("Failed to retrieve memory info. Error code: %d", GetLastError());
    }
}

int main() {
    DbgLog("Process ID: %d", GetCurrentProcessId());
    const size_t allocationSize = 4096 * 500000;
    const size_t PAGE_SIZE = 4096;
    const size_t NUM_PAGES = allocationSize / PAGE_SIZE;
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    // [1] 初期ブレーク
    PrintMemoryUsage();
    DebugBreak(); 

    LPVOID p1 = VirtualAlloc(NULL, allocationSize, MEM_RESERVE, PAGE_READONLY);
    if (!p1) return 1;
    DbgLog("Allocated at: 0x%p\n", p1);
    DbgLog("--- MEM_RESERVE ---\n");
    PrintMemoryUsage();
    DebugBreak(); 

    LPVOID p2 = VirtualAlloc(p1, allocationSize, MEM_COMMIT, PAGE_READWRITE);
    DbgLog("--- MEM_COMMIT ---\n");
    PrintMemoryUsage();
    DebugBreak(); 
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    for (size_t i = 0; i < NUM_PAGES; ++i) {
        ((unsigned char*)p1)[i * PAGE_SIZE] = (unsigned char)(i & 0xFF);
    }
    DbgLog("--- After Data Write (All) ---\n");
    PrintMemoryUsage();
    DebugBreak(); 

    for (size_t i = 0; i < NUM_PAGES; ++i) {
        // ページごとに属性をバラバラにする
        DWORD old;
        DWORD targetProtect;
        switch (i % 2) {
            case 0: targetProtect = PAGE_NOACCESS; break;
            case 1: targetProtect = PAGE_READWRITE | PAGE_GUARD; break;
        }
        VirtualProtect((LPVOID)((uintptr_t)p1 + (i * PAGE_SIZE)), PAGE_SIZE, targetProtect, &old);

        // 10000ページごとに進捗を出力（出しすぎるとVMが止まるため）
        if (i % 10000 == 0) DbgLog("Allocated %zu pages...\n", i);
    }

    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    
    DbgLog("--- After Working Set Trim 1---\n");
    PrintMemoryUsage();
    DebugBreak();
    //次は逆にしてもう一周
    for (size_t i = 0; i < NUM_PAGES; ++i) {
        // ページごとに属性をバラバラにする
        DWORD old;
        DWORD targetProtect;
        switch (i % 2) {
            case 0: targetProtect = PAGE_READWRITE | PAGE_GUARD; break;
            case 1: targetProtect = PAGE_NOACCESS; break;
        }
        VirtualProtect((LPVOID)((uintptr_t)p1 + (i * PAGE_SIZE)), PAGE_SIZE, targetProtect, &old);

        // 10000ページごとに進捗を出力（出しすぎるとVMが止まるため）
        if (i % 10000 == 0) DbgLog("Allocated %zu pages...\n", i);
    }

    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    
    DbgLog("--- After Working Set Trim 2---\n");
    PrintMemoryUsage();
    DebugBreak();

    VirtualFree(p1, 0, MEM_RELEASE);
    return 0;
}