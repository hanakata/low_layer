#include <windows.h>
#include <iostream>
#include <iomanip>

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
    std::cout << buffer << std::endl;
}

int main() {
    DbgLog("--- PAGE_GUARD Observation Tool ---");
    DbgLog("Process ID: %d", GetCurrentProcessId());

    // [1] 初期ブレーク
    DebugBreak(); 

    // 3ページ分確保
    LPVOID p1 = VirtualAlloc(NULL, 4096 * 3, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p1) return 1;

    // [2] 物理ページを割り当てるためにタッチ
    memset(p1, 0xAA, 4096 * 3);
    DbgLog("Allocated at: 0x%p", p1);

    // [3] ガード設定前の状態を確認
    DebugBreak();

    DWORD old;
    BYTE* target_page0 = static_cast<BYTE*>(p1);
    BYTE* target_page1 = static_cast<BYTE*>(p1) + 4096;
    BYTE* target_page2 = static_cast<BYTE*>(p1) + 4096 + 4096;

    if (VirtualAlloc(target_page0, 4096, MEM_RESET, PAGE_READWRITE)) {
        DbgLog("VirtualAlloc (MEM_RESET) Succeeded!");
    } else {
        DbgLog("VirtualAlloc Failed! Error: %d", GetLastError());
        return 1;
    }
    
    if (VirtualProtect(target_page1, 4096, PAGE_READWRITE | PAGE_GUARD, &old)) {
        DbgLog("VirtualProtect (PAGE_READWRITE | PAGE_GUARD) Succeeded!");
    } else {
        DbgLog("VirtualProtect Failed! Error: %d", GetLastError());
        return 1;
    }

    if (VirtualProtect(target_page2, 4096, PAGE_NOACCESS, &old)) {
        DbgLog("VirtualProtect (PAGE_NOACCESS) Succeeded!");
    } else {
        DbgLog("VirtualProtect Failed! Error: %d", GetLastError());
        return 1;
    }
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(target_page1, &mbi, sizeof(mbi));
    DbgLog("API Report - State: 0x%X, Protect: 0x%X", mbi.State, mbi.Protect);

    // [4] ガード設定後の PTE/VAD を確認
    // WinDbg: !pte target_page / !vad target_page
    DebugBreak();

    __try {
        DbgLog("[5] Triggering PAGE_GUARD...");
        *target_page1 = 0xFF; // 書き込みによるガード剥がし
    }
    // 修正ポイント：STATUS_GUARD_PAGE_VIOLATION をキャッチする
    __except (GetExceptionCode() == STATUS_GUARD_PAGE_VIOLATION ? 
              EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        
        DbgLog("[SUCCESS] STATUS_GUARD_PAGE_VIOLATION caught!");
        DbgLog("Exception Code: 0x%x.", GetExceptionCode());
    }
    
    VirtualQuery(target_page1, &mbi, sizeof(mbi));
    DbgLog("API Report - State: 0x%X, Protect: 0x%X", mbi.State, mbi.Protect);

    // [6] ガード剥がれ後の PTE/VAD / VirtualQuery の確認
    DbgLog("Check attributes after violation. Is it back to PAGE_READWRITE?");
    DebugBreak();

    *target_page1 = 0xFF;
    DbgLog("Check !vad & !pte");
    DebugBreak();

    DbgLog("--- Testing MEM_RESET_UNDO ---");

    VirtualQuery(target_page0, &mbi, sizeof(mbi));
    DbgLog("API Report (target_page0) - State: 0x%X, Protect: 0x%X", mbi.State, mbi.Protect);
    // target_page0 に対して UNDO を実行
    if (VirtualAlloc(target_page0, 4096, MEM_RESET_UNDO, PAGE_READWRITE)) {
        DbgLog("VirtualAlloc (MEM_RESET_UNDO) Succeeded!");
    } else {
        DbgLog("VirtualAlloc (MEM_RESET_UNDO) Failed! Error: %d", GetLastError());
    }
    *target_page0 = 0xAA;

    // 属性の変化を確認
    VirtualQuery(target_page0, &mbi, sizeof(mbi));
    DbgLog("API Report (target_page0) - State: 0x%X, Protect: 0x%X", mbi.State, mbi.Protect);

    // [7] MEM_RESET_UNDO 後の PTE を確認
    DebugBreak();

    VirtualFree(p1, 0, MEM_RELEASE);
    return 0;
}