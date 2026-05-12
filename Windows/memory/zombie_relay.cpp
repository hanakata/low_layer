#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <string>
#include <vector>
#pragma comment(lib, "psapi.lib")

void DbgLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

void PrintBothMemoryUsage(const wchar_t* stageName) {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));

    DbgLog("===== [ Memory Comparison: %S ] =====\n", stageName);
    // 共有メモリの場合、PrivateUsageではなくWorkingSetSizeの変動に注目
    DbgLog("Private: %zu KB | WorkingSet: %zu KB\n", pmc.PrivateUsage / 1024, pmc.WorkingSetSize / 1024);
    DbgLog("==============================================\n\n");
}


// 疑似コード: メイン関数の引数設計
int wmain(int argc, wchar_t* argv[]) {
    // 世代管理ロジック（既存）
    int generation = 1;
    if (argc >= 3) {
        generation = std::stoul(argv[2]);
    }

    std::wstring mySectionName = L"Local\\MySharedMemory_Gen" + std::to_wstring(generation);
    std::wstring parentSectionName = L"Local\\MySharedMemory_Gen" + std::to_wstring(generation - 1);
    std::wstring readyEventName = L"Local\\Event_Gen" + std::to_wstring(generation) + L"_Ready";
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    std::vector<HANDLE> hLegacyHandles;
    for (int i = 1; i < generation; ++i) {
        std::wstring name = L"Local\\MySharedMemory_Gen" + std::to_wstring(i);
        HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (h) {
            MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0); // 空間に縛り付ける
            hLegacyHandles.push_back(h);
        }
    }

    // 2. 自分のセクションを作成して物理メモリを汚す
    const size_t allocationSize = 1024 * 1024 * 500;
    HANDLE hMyMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)allocationSize, mySectionName.c_str());
    LPVOID pMyMem = MapViewOfFile(hMyMap, FILE_MAP_ALL_ACCESS, 0, 0, allocationSize);
    memset(pMyMem, 0x41, allocationSize);

    DbgLog("[Gen %d] Allocated 500MB. Ready for next...\n", generation);
    DebugBreak();

    // 3. 次の世代のためのイベント準備と起動
    std::wstring nextEventName = L"Local\\Event_Gen" + std::to_wstring(generation + 1) + L"_Ready";
    HANDLE hNextReadyEvent = CreateEventW(NULL, FALSE, FALSE, nextEventName.c_str());

    std::wstring cmdLine = L"zombie_relay.exe " + std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(generation + 1);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };



    if (CreateProcessW(L"zombie_relay.exe", &cmdLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        // 【重要】子がセクションを掴むまで、死なずに待つ
        DbgLog("[Gen %d] Waiting for Gen %d to catch my legacy...\n", generation, generation + 1);
        WaitForSingleObject(hNextReadyEvent, 30000); // 30秒待機
        
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    CloseHandle(hNextReadyEvent);
    return 0;
}