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
    const size_t allocationSize = 4096 * 100;
    // [1] 初期ブレーク
    PrintMemoryUsage();
    DebugBreak(); 

    LPVOID p1 = VirtualAlloc(NULL, allocationSize, MEM_RESERVE, PAGE_NOACCESS);
    if (!p1) return 1;
    DbgLog("Allocated at: 0x%p\n", p1);
    DbgLog("--- MEM_RESERVE ---\n");
    PrintMemoryUsage();
    DebugBreak(); 

    LPVOID p2 = VirtualAlloc(p1, allocationSize, MEM_COMMIT, PAGE_READWRITE);
    DbgLog("--- MEM_COMMIT ---\n");
    PrintMemoryUsage();
    DebugBreak(); 

    BYTE* p = static_cast<BYTE*>(p1);
    p[0] = 0xFF; // 最初の1ページのみ物理メモリに載る
    DbgLog("--- After Data Write (part) ---\n");
    PrintMemoryUsage();
    DebugBreak(); 

    SecureZeroMemory(p1, allocationSize); 
    DbgLog("--- After Data Write (All) ---\n");
    PrintMemoryUsage();
    DebugBreak(); 
    BYTE* reset_target_page = static_cast<BYTE*>(p1) + 4096 * 2;
    BYTE* guard_target_page = static_cast<BYTE*>(p1) + 4096 * 3;
    BYTE* read_only_page = static_cast<BYTE*>(p1) + 4096 * 9;
    BYTE* noaccess_page = static_cast<BYTE*>(p1) + 4096 * 10;

    VirtualAlloc(reset_target_page, 4096, MEM_RESET, PAGE_READWRITE);
    DbgLog("--- After MEM_RESET ---\n");
    PrintMemoryUsage();
    DebugBreak();
    DWORD old;
    VirtualProtect(guard_target_page, 4096, PAGE_READWRITE | PAGE_GUARD, &old);
    DbgLog("--- After PAGE_GUARD ---\n");
    PrintMemoryUsage();
    DebugBreak();

    VirtualAlloc(read_only_page, 4096, MEM_COMMIT, PAGE_READONLY);
    DbgLog("--- After PAGE_READONLY ---\n");
    PrintMemoryUsage();
    DebugBreak();

    VirtualAlloc(noaccess_page, 4096, MEM_COMMIT, PAGE_NOACCESS);
    DbgLog("--- After NOACCESS ---\n");
    PrintMemoryUsage();
    DebugBreak();

    // [5] Working Set Trim の実行
    // 引数に -1 を渡すと、可能な限りのページをワーキングセットから削除（物理メモリから解放）する
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    
    DbgLog("--- After Working Set Trim ---\n");
    PrintMemoryUsage();
    DebugBreak();

    // [6] 再アクセス（再ロードの確認）
    for(int i = 0; i < 5; ++i) {
        __try {
            p[i * 4096] = (BYTE)i; 
        }
        __except (GetExceptionCode() == STATUS_GUARD_PAGE_VIOLATION ? 
              EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        
            DbgLog("[SUCCESS] STATUS_GUARD_PAGE_VIOLATION caught!\n");
            DbgLog("Exception Code: 0x%x.\n", GetExceptionCode());
        }
        DbgLog("--- After Data Write (Page %d) ---\n", i);
        PrintMemoryUsage();
        DebugBreak();
    }

    VirtualFree(p1, 0, MEM_RELEASE);
    return 0;
}