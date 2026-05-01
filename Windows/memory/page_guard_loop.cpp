#include <windows.h>
#include <stdio.h>
#include <iostream>

// WinDbgから 'ed g_mode 3' のようにして動的に変更可能にする
volatile DWORD g_mode = 1; 
volatile UINT64 g_loop_count = 0;
// ターゲットのアドレスを保存するグローバル
void* g_target_addr = nullptr;

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

LONG CALLBACK GuardPageHandler(PEXCEPTION_POINTERS ExceptionInfo) {
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;
    PCONTEXT ctx = ExceptionInfo->ContextRecord;
    g_loop_count++;

    // 1000回ごとにDbgPrint（KDに表示される）
    if (g_loop_count % 10000000 == 0) {
        DbgLog("[VEH] Count: %llu, Mode: %d, RIP: %p\n", g_loop_count, g_mode, ctx->Rip);
    }
    DWORD old;
    // --- [パターン1] PAGE_GUARD 発生 ---
    if (rec->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION) {
        g_target_addr = (void*)rec->ExceptionInformation[1];

        if (g_mode == 1) {
            // 死のループ（現状通り）
            VirtualProtect(g_target_addr, 1, PAGE_READWRITE | PAGE_GUARD, &old);
            return EXCEPTION_CONTINUE_EXECUTION;
        } 
        else if (g_mode == 2) {
            // 書き込んで進める（1命令だけガードを外す）
            VirtualProtect(g_target_addr, 1, PAGE_READWRITE, &old);
            ctx->EFlags |= 0x100; // TF(Trap Flag)をセット
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    // --- [パターン2] 1命令実行完了後のトラップ ---
    if (rec->ExceptionCode == STATUS_SINGLE_STEP && g_mode == 2) {
        // 命令が1つ終わった瞬間にここに来る。ここでガードを「再装填」
        if (g_target_addr) {
            VirtualProtect(g_target_addr, 1, PAGE_READWRITE | PAGE_GUARD, &old);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

int main() {
    AddVectoredExceptionHandler(1, GuardPageHandler);

    void* pPage = VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE);
    *(DWORD*)pPage = 0x12345678;

    // KDでこのアドレスを確認できるよう出力
    DbgLog("Target Page: %p, g_mode address: %p\n", pPage, &g_mode);
    DebugBreak(); 
    // [実験開始] 
    // ここでPAGE_GUARDを設定
    DWORD old;
    VirtualProtect(pPage, 4096, PAGE_READWRITE | PAGE_GUARD, &old);

    // インラインアセンブリで命令サイズを固定（x64用）
    // 確実に 7バイトの MOV 命令にするためのダミー
    // mov dword ptr [rcx], 0xDEADBEEF -> 48 c7 01 ef be ad de (7 bytes)
    printf("Triggering...\n");
    *(volatile DWORD*)pPage = 0xDEADBEEF; 

    DbgLog("Final loop count: %llu\n", g_loop_count);
    return 0;
}