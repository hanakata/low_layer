#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    
    // WinDbgのコンソールに即時出力される
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    
    // VM内のコンソールにも一応出しておく（g で進んだ後に見える）
    std::cout << buffer << std::endl;
}

int main() {
    std::cout << "--- VAD Split Observation Tool (Stabilized) ---" << std::endl;
    std::cout << "Process ID: " << GetCurrentProcessId() << std::endl;
    DbgLog("--- VAD Split Observation Tool (Stabilized) ---");
    DbgLog("Process ID: %d", GetCurrentProcessId());

    // 1. デバッガを強制的に呼び出す
    // 実行すると WinDbg がこの場所で自動的にブレークします
    std::cout << "[1] Triggering DebugBreak... WinDbg will catch this." << std::endl;
    DbgLog("[1] Triggering DebugBreak... WinDbg will catch this.");
    DebugBreak(); 

    // 2. メモリ確保 (3ページ分)
    LPVOID p1 = VirtualAlloc(NULL, 4096 * 3, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p1 == NULL) return 1;

    // --- 重要: 物理メモリを確定させる (Touch) ---
    // これをしないと VirtualProtect が不安定になることがあります
    std::cout << "[2] Touching memory to commit physical pages..." << std::endl;
    DbgLog("[2] Touching memory to commit physical pages...");
    memset(p1, 0xAA, 4096 * 3);

    std::cout << "    Allocated at: 0x" << std::setw(16) << std::setfill('0') << std::hex << p1 << std::endl;
    DbgLog("    Allocated at: 0x%016llx", (unsigned long long)p1);
    // 3. ここでもう一度止める (VAD 分割前の確認用)
    std::cout << "[3] Breaking before VirtualProtect... Check !vad now." << std::endl;
    DbgLog("[3] Breaking before VirtualProtect... Check !vad now.");
    DebugBreak();

    // 4. VAD分割の実行
    DWORD old;
    std::cout << "[4] Changing protection of the middle page to PAGE_READONLY..." << std::endl;
    DbgLog("[4] Changing protection of the middle page to PAGE_READONLY...");
    BYTE* base = reinterpret_cast<BYTE*>(p1);
    LPVOID target_page = static_cast<LPVOID>(base + 4096);
    if (VirtualProtect(target_page, 4096, PAGE_READONLY, &old)) {
        std::cout << "    VirtualProtect Succeeded!" << std::endl;
        DbgLog("    VirtualProtect Succeeded!");
    } else {
        std::cout << "    VirtualProtect Failed! Error: " << GetLastError() << std::endl;
        DbgLog("    VirtualProtect Failed! Error: ");
    }

    // 5. 分割後の確認用
    std::cout << "[5] VAD Split completed! Check !vad again." << std::endl;
    DbgLog("[5] VAD Split completed! Check !vad again.");
    DebugBreak();

    // 5.1 真実の瞬間（SEHで保護）
    __try {
        std::cout << "[5.1] Attempting to write to READONLY page..." << std::endl;
        
        // 書き込み実行
        *reinterpret_cast<BYTE*>(target_page) = 0xFF;

        // ここに来たら「失敗」
        std::cout << "[!!!] Error: Wrote to READONLY memory without exception!" << std::endl;
        DbgLog("[!!!] Error: Wrote to READONLY memory without exception!");
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? 
              EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        
        // 例外をキャッチした場合
        std::cout << "[SUCCESS] Access Violation caught as expected!" << std::endl;
        std::cout << "Exception Code: 0x" << std::hex << GetExceptionCode() << std::endl;
        DbgLog("[SUCCESS] Access Violation caught as expected!");
        DbgLog("Exception Code: 0x%x.", GetExceptionCode());
    }

    std::cout << "\n[6] Re-applying VirtualProtect across page boundaries..." << std::endl;

    // 全体をRWに戻す（ここでVADが再び統合されるか？の観測ポイント）
    VirtualProtect(base, 4096 * 3, PAGE_READWRITE, &old);
    DbgLog("VAD PAGE_READWRITE Change completed! Check !vad again.");
    DebugBreak();

    // 2ページ目だけ NA にして不均一な状態を作る
    VirtualProtect(base + 4096, 4096, PAGE_NOACCESS, &old);
    DbgLog("VAD PAGE_NOACCESS Change completed! Check !vad again.");
    DebugBreak();

    // 【本番】境界またぎの実行
    if (VirtualProtect(base + 2048, 4096, PAGE_READONLY, &old)) {
        // base+2048(Page0) の属性が返ってくるか確認
        std::cout << "Old Protection from Page0: 0x" << std::hex << old << std::endl;
        DbgLog("Old Protection from Page0: 0x%x. Check !vad and !pte again.", old);
    }

    // ここで DebugBreak() して !pte と !vad を最終確認
    DebugBreak();

    // 7. クリーンアップ（例外が起きてもここに来れる）
    if (p1) {
        VirtualFree(p1, 0, MEM_RELEASE);
        std::cout << "[7] Memory freed. Exiting gracefully." << std::endl;
    }

    return 0;
}