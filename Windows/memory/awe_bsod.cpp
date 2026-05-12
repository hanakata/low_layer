#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <iomanip>
#include <string>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Advapi32.lib")
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

bool EnableLockMemoryPrivilege() {
    HANDLE hToken;
    // 1. プロセスの「トークン」を開く
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    // 2. "SeLockMemoryPrivilege" という文字列をシステム内部ID（LUID）に変換
    LUID luid;
    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }

    // 3. 有効化したい権限の情報を構造体にセット
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; // ここで「有効」を指定

    // 4. トークンの設定を更新（これが真の AdjustTokenPrivileges）
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        CloseHandle(hToken);
        return false;
    }

    // AdjustTokenPrivilegesは「一部失敗」でもTRUEを返すため、GetLastErrorを確認
    bool success = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    return success;
}

int main() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const DWORD pageSize = si.dwPageSize;
    const ULONG_PTR numberOfPages = 100;
    const size_t totalSize = pageSize * numberOfPages;
    // 1. 特権の有効化 (AdjustTokenPrivileges) がここに必要
    // ※前述の「儀式」を済ませている前提
    DebugBreak();
    EnableLockMemoryPrivilege();

    // 2. AWE用の「窓」を仮想アドレス空間に予約
    // MEM_COMMITは使わない。物理メモリとの紐付けは後で行うため。
    LPVOID pAWE = VirtualAlloc(NULL, totalSize, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
    if (!pAWE) return 1;

    // 3. 実際の物理ページを確保
    ULONG_PTR actualPages = numberOfPages;
    ULONG_PTR pfnArray[100]; 
    if (!AllocateUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray)) {
        VirtualFree(pAWE, 0, MEM_RELEASE);
        return 2;
    }

    ULONG_PTR pfnArray2[100]; 
    if (!AllocateUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray2)) {
        VirtualFree(pAWE, 0, MEM_RELEASE);
        return 2;
    }

    // 4. 「窓」に物理ページをガチャンとはめる
    if (!MapUserPhysicalPages(pAWE, actualPages, pfnArray)) {
        FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray);
        VirtualFree(pAWE, 0, MEM_RELEASE);
        return 3;
    }

    // --- ここで pAWE に対して読み書きが可能になる ---
    strcpy((char*)pAWE, "AWE Memory Access Success!");

    if (!MapUserPhysicalPages(pAWE, actualPages, pfnArray2)) {
        FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray2);
        VirtualFree(pAWE, 0, MEM_RELEASE);
        return 3;
    }
    
    // --- ここで pAWE に対して読み書きが可能になる ---
    strcpy((char*)pAWE, "AWE2 Memory Access Success!");
    DbgLog("Check PFN: %d\n",pfnArray[0]);
    DbgLog("Check PFN2: %d\n",pfnArray2[0]);
    DebugBreak();

    LPVOID pAWE2 = VirtualAlloc(NULL, totalSize, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
    if (!pAWE2) return 1;
    // 4. 「窓」に物理ページをガチャンとはめる
    if (!MapUserPhysicalPages(pAWE2, actualPages, pfnArray)) {
        FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray);
        VirtualFree(pAWE2, 0, MEM_RELEASE);
        return 3;
    }

    DbgLog("Check PFN: %d\n",pfnArray[0]);
    DbgLog("Check PFN2: %d\n",pfnArray2[0]);
    DebugBreak();

    strcpy((char*)pAWE, "AWE Memory Re-Access Success!");
    DbgLog("Check PFN: %d\n",pfnArray[0]);
    DbgLog("Check PFN2: %d\n",pfnArray2[0]);
    DebugBreak();

    // 5. 後片付け (逆順)
    MapUserPhysicalPages(pAWE, actualPages, NULL); // マップ解除
    FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray); // 物理解放
    FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray2); // 物理解放
    VirtualFree(pAWE, 0, MEM_RELEASE); // 仮想空間解放

    return 0;
}