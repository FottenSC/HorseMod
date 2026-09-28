#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include "GameImGui/ReplayOutputWindow.hpp"

int main(){
    Horse::GameImGui::ReplayOutputWindow output;
    const bool opened=output.open(320,200);
    const auto hwnd=output.hwnd();
    const auto style=hwnd?GetWindowLongPtrW(hwnd,GWL_STYLE):0;
    const auto extended=hwnd?GetWindowLongPtrW(hwnd,GWL_EXSTYLE):0;
    DWORD pid{};const auto thread=hwnd?GetWindowThreadProcessId(hwnd,&pid):0;
    DWORD_PTR activation{},hit{};
    const bool responsive=opened && SendMessageTimeoutW(hwnd,WM_MOUSEACTIVATE,0,0,SMTO_ABORTIFHUNG,250,&activation)
        && SendMessageTimeoutW(hwnd,WM_NCHITTEST,0,MAKELPARAM(0,0),SMTO_ABORTIFHUNG,250,&hit);
    const bool interactive=responsive && !(style&WS_DISABLED) && !(extended&(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT))
        && activation==MA_ACTIVATE && hit==HTCLIENT && thread!=GetCurrentThreadId() && pid==GetCurrentProcessId();
    std::printf("owned replay UI interactive=%s responsive=%d style=%llx exstyle=%llx activation=%llu hit=%llu\n",
        interactive?"PASS":"FAIL",responsive,static_cast<unsigned long long>(style),static_cast<unsigned long long>(extended),activation,hit);
    DWORD_PTR ignored{};
    const bool close_guard=SendMessageTimeoutW(hwnd,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,250,&ignored)
        && IsWindow(hwnd) && output.active() && output.hwnd()==hwnd;
    std::printf("external close preserves acquired output=%d\n",close_guard);
    const bool closed=output.close();const bool twice=output.close();
    std::printf("owned replay UI closed=%d repeated_cleanup=%d stale_window=%d\n",closed,twice,hwnd?IsWindow(hwnd):0);
    return interactive && close_guard && closed && twice && !output.active() && !output.hwnd() && !IsWindow(hwnd)?0:1;
}
