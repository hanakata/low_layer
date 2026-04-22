#include <windows.h>
#include <iostream>

int main() {
    std::cout << "Starting Memory Experiment..." << std::endl;
    system("pause"); // ここで WinDbg をアタッチして bp を張る

    // 1. まず 64KB 予約 (RESERVE) する
    LPVOID p1 = VirtualAlloc(NULL, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    std::cout << "Allocated at: " << p1 << std::endl;

    // 2. 同じ場所に、もう一度別のサイズで Alloc しようとしてみる (衝突実験)
    // p1 を第一引数に指定することで、意図的に衝突を狙う
    LPVOID p2 = VirtualAlloc(p1, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
    if (p2 == NULL) {
        std::cout << "Collision Error! GetLastError: " << GetLastError() << std::endl;
    }else{
        std::cout << "Allocated at: " << p2 << std::endl;
    }

    // 3. 解放 (RELEASE)
    VirtualFree(p1, 0, MEM_RELEASE);
    std::cout << "Memory Released." << std::endl;

    return 0;
}