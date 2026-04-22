#include <windows.h>
#include <iostream>
#include <cstdio>
#include <vector>

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    std::cout << buffer << std::endl;
}

int main() {
    const size_t TOTAL_SIZE = 1024 * 1024 * 1024; // 1GB
    const size_t PAGE_SIZE = 4096;               // 4KB (1ページ)
    const size_t NUM_PAGES = TOTAL_SIZE / PAGE_SIZE;

    DbgLog("Starting VAD Fragmentation Attack...");
    DbgLog("Target: %zu independent VAD nodes", NUM_PAGES);
    //DebugBreak(); 

    std::vector<void*> pages;
    pages.reserve(NUM_PAGES);

    // 1. 1ページずつ「独立して」VirtualAllocを叩く
    // これにより、OSは結合を試みるが、属性が違うと結合できない
    for (size_t i = 0; i < NUM_PAGES; ++i) {
        // MEM_RESERVE | MEM_COMMIT を別々に呼ぶことで管理構造を強制
        void* p = VirtualAlloc(NULL, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) {
            pages.push_back(p);
            
            // ページごとに属性をバラバラにする (RW, RO, NOACCESSのローテーション)
            DWORD old;
            DWORD targetProtect;
            switch (i % 3) {
                case 0: targetProtect = PAGE_READONLY; break;
                case 1: targetProtect = PAGE_NOACCESS; break;
                case 2: targetProtect = PAGE_EXECUTE_READ; break;
            }
            VirtualProtect(p, PAGE_SIZE, targetProtect, &old);

            if (i % 3 != 1) {
                // volatileを付けることでコンパイラの最適化（読み飛ばし）を防ぐ
                unsigned char value = *(volatile unsigned char*)p;
            }

            // 10000ページごとに進捗を出力（出しすぎるとVMが止まるため）
            if (i % 10000 == 0) DbgLog("Allocated %zu pages...", i);
        }
    }

    DbgLog("Allocation complete. Check !vad and !poolused (Nonpaged pool).");
    DbgLog("VAD tree level should be insane now.");
    //DebugBreak(); 

    // 2. さらに追い打ち：ランダムに一部をDECOMMITしてPTEを虫食い状態にする
    for (size_t i = 0; i < pages.size(); i += 7) { // 7ページおき
        VirtualFree(pages[i], PAGE_SIZE, MEM_DECOMMIT);
    }

    DbgLog("Decommit complete. If the system is still alive, your Windows is a tank.");
    //DebugBreak();

    // 3. 最後に一気に解放してカーネルの再構築処理（Cleanup）に負荷をかける
    for (void* p : pages) {
        VirtualFree(p, 0, MEM_RELEASE);
    }

    return 0;
}