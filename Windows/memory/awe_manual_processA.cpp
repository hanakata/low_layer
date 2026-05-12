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

    // 4. 「窓」に物理ページをガチャンとはめる
    if (!MapUserPhysicalPages(pAWE, actualPages, pfnArray)) {
        FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray);
        VirtualFree(pAWE, 0, MEM_RELEASE);
        return 3;
    }

    // --- ここで pAWE に対して読み書きが可能になる ---
    strcpy((char*)pAWE, "[Process A] AWE Memory Access Success!");
    DbgLog("Check PFN: %d\n",pfnArray[0]);
    DbgLog("Data in AWE: %s\n", pAWE);
    DebugBreak();

    // 3. プロセスBの起動（引数にはPIDのみ渡す。アドレスは各自マッピングするため不要だが、同期用にPIDを渡す）
    std::wstring cmdLine = L"awe_manual_processB.exe " + std::to_wstring(GetCurrentProcessId());

    STARTUPINFOW si2 = { sizeof(si2) };
    PROCESS_INFORMATION pi = { 0 };

    BOOL success = CreateProcessW(
        L"awe_manual_processB.exe", &cmdLine[0],
        NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi
    );

    if (!success) {
        DbgLog("[Process A] CreateProcess failed.\n");
        return 1;
    }
    // 子プロセスの終了を待機
    WaitForSingleObject(pi.hProcess, INFINITE);

    DbgLog("Data in AWE: %s\n", pAWE);
    DebugBreak();

    // 5. 後片付け (逆順)
    MapUserPhysicalPages(pAWE, actualPages, NULL); // マップ解除
    FreeUserPhysicalPages(GetCurrentProcess(), &actualPages, pfnArray); // 物理解放
    VirtualFree(pAWE, 0, MEM_RELEASE); // 仮想空間解放

    return 0;
}