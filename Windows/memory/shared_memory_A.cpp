#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <string>

#pragma comment(lib, "psapi.lib")

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

int main() {
    const size_t allocationSize = 4096 * 100; // 400KB
    const wchar_t* szMappingName = L"Local\\MySharedMemoryVerify";
    DebugBreak();
    // 1. ページファイルをバックにする名前付きファイルマッピングオブジェクトの作成
    // この時点では、物理メモリへの実割り当て（Working Set）は発生しない
    HANDLE hMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,   // ページファイルを使用
        NULL,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(allocationSize),
        szMappingName
    );

    if (hMapFile == NULL) {
        DbgLog("[Process A] CreateFileMappingW failed. Error: %d\n", GetLastError());
        return 1;
    }

    // 2. 自プロセスの仮想アドレス空間にマッピング
    LPVOID pLocalMem = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, allocationSize);
    if (!pLocalMem) {
        CloseHandle(hMapFile);
        return 1;
    }

    DbgLog("[Process A] Process ID: %d, Mapped Address: %p\n", GetCurrentProcessId(), pLocalMem);
    DebugBreak();

    const size_t allocSize = 4096 * 100;
    memset(pLocalMem, 0x41, allocSize);
    DbgLog("[Process A] 0. After Process A Writes\n");
    DebugBreak();

    // 3. プロセスBの起動（引数にはPIDのみ渡す。アドレスは各自マッピングするため不要だが、同期用にPIDを渡す）
    std::wstring cmdLine = L"shared_memory_B.exe " + std::to_wstring(GetCurrentProcessId());

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    BOOL success = CreateProcessW(
        L"shared_memory_B.exe", &cmdLine[0],
        NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );

    if (!success) {
        DbgLog("[Process A] CreateProcess failed.\n");
        UnmapViewOfFile(pLocalMem);
        CloseHandle(hMapFile);
        return 1;
    }
    // 子プロセスの終了を待機
    WaitForSingleObject(pi.hProcess, INFINITE);

    // クリーンアップ
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    UnmapViewOfFile(pLocalMem);
    CloseHandle(hMapFile);

    return 0;
}