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

    void* pA = VirtualAlloc(NULL, TOTAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (pA == NULL) return 1;

    DbgLog("    Allocated at: 0x%016llx", (unsigned long long)pA);

    // 2. 一部だけ属性変更
    for (size_t i = 0; i < TOTAL_SIZE; i += CHUNK_SIZE * 2) {
        DWORD old;
        VirtualProtect((char*)pA + i, CHUNK_SIZE, PAGE_READONLY, &old);
        volatile char temp = *(char*)pA;
    }
    DbgLog("Check !vad and !address now.");
    DebugBreak(); 

    // 2. 5回に1回のペースで RELEASE (土地を返却)
    for (size_t i = 0; i < TOTAL_SIZE; i += CHUNK_SIZE) {
        if ((i / CHUNK_SIZE) % 5 == 0) {
            // 特定の区画だけ「予約解除」
            VirtualFree((char*)pA + i, CHUNK_SIZE, MEM_RELEASE);
            DbgLog("RELEASED at: 0x%zx", i);
        }
    }

    DbgLog("Check !vad and !address now.");
    DebugBreak(); 
}