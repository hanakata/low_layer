#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>

void DumpRemoteMemoryInfo(DWORD pid, uintptr_t targetAddr) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) {
        std::cerr << "OpenProcess failed. GetLastError: " << GetLastError() << std::endl;
        return;
    }

    MEMORY_BASIC_INFORMATION mbi;
    // VirtualQueryEx で外部プロセスのVAD情報を直接 OS に問い合わせる
    if (VirtualQueryEx(hProcess, (LPCVOID)targetAddr, &mbi, sizeof(mbi))) {
        std::cout << "--- OS Memory Report ---" << std::endl;
        std::cout << "Target Address: 0x" << std::hex << targetAddr << std::endl;
        std::cout << "State:          0x" << mbi.State << " (" 
                  << (mbi.State == MEM_FREE ? "MEM_FREE" : (mbi.State == MEM_RESERVE ? "MEM_RESERVE" : "MEM_COMMIT")) 
                  << ")" << std::endl;
        
        // ここが今回の核心。FREE の時、OS は 0 を返すのか 1(PAGE_NOACCESS) を返すのか
        std::cout << "Protect:        0x" << mbi.Protect << std::endl;
        std::cout << "AllocationBase: 0x" << mbi.AllocationBase << std::endl;
        std::cout << "RegionSize:     0x" << std::dec << mbi.RegionSize << " bytes" << std::endl;
    } else {
        std::cerr << "VirtualQueryEx failed. GetLastError: " << GetLastError() << std::endl;
    }

    CloseHandle(hProcess);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: memcheck.exe <PID> <Address(hex)>" << std::endl;
        return 1;
    }

    DWORD pid = std::stoul(argv[1]);
    uintptr_t addr = std::stoull(argv[2], nullptr, 16);

    DumpRemoteMemoryInfo(pid, addr);
    return 0;
}