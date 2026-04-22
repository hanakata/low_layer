#include <windows.h>
#include <iostream>
#include <cstdio>

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
    const size_t TOTAL_SIZE = 1024 * 1024 * 1024; // 1GB
    const size_t CHUNK_SIZE = 0x2000000;          // 32MB (ちょうど32分割)
    DebugBreak(); 

    // 1. 全体を RESERVE のみ
    void* pB = VirtualAlloc(NULL, TOTAL_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (pB == NULL) return 1;

    DbgLog("    Allocated at: 0x%016llx", (unsigned long long)pB);

    // 2. 交互に COMMIT
    for (size_t i = 0; i < TOTAL_SIZE; i += CHUNK_SIZE) {
        DWORD prot = ((i / CHUNK_SIZE) % 2 == 0) ? PAGE_READONLY : PAGE_READWRITE;
        VirtualAlloc((char*)pB + i, CHUNK_SIZE, MEM_COMMIT, prot);

        // 【重要】ここで「読み取り」アクセスをして物理ページを確定させる
        volatile char temp = *((char*)pB + i);
    }
    DbgLog("Check !vad and !address now.");
    DebugBreak(); 

    // 2. 5回に1回のペースで RELEASE (土地を返却)
    for (size_t i = 0; i < TOTAL_SIZE; i += CHUNK_SIZE) {
        if ((i / CHUNK_SIZE) % 5 == 0) {
            // 特定の区画だけ「予約解除」
            VirtualFree((char*)pB + i, CHUNK_SIZE, MEM_RELEASE);
            DbgLog("RELEASED at: 0x%zx", i);
        }
    }

    DbgLog("Check !vad and !address now.");
    DebugBreak(); 

}