// Bounded development diagnostic. Launches only the supplied executable,
// records startup/exit stacks, and always reaps its own debuggee. No dumps.
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#pragma comment(lib, "Dbghelp.lib")

struct Breakpoint { BYTE original; std::string name; };

static void Stack(HANDLE process, HANDLE thread) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread, &context)) return;
    std::printf("context rip=%llx rcx=%llx rdx=%llx\n", context.Rip, context.Rcx, context.Rdx);
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    for (unsigned i = 0; i != 24; ++i) {
        const auto base = SymGetModuleBase64(process, frame.AddrPC.Offset);
        std::printf("stack %u pc=%llx module=%llx rva=%llx\n", i,
            frame.AddrPC.Offset, base, frame.AddrPC.Offset - base);
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
            &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path executable = std::filesystem::absolute(argv[1]);
    const auto directory = executable.parent_path().wstring();
    std::wstring command = L"\"" + executable.wstring()
        + L"\" -HorseQualificationRun=startup-exit-trace";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, directory.c_str(),
        &startup, &child)) { std::printf("launch_error=%lu\n", GetLastError()); return 2; }
    std::printf("owned_pid=%lu\n", child.dwProcessId);
    std::fflush(stdout);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    SymInitialize(child.hProcess, "", FALSE);
    std::map<DWORD, HANDLE> threads;
    std::map<ULONG_PTR, Breakpoint> breakpoints;
    bool alive = true, killed = false;
    DWORD result = 2;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (alive) {
        if (!killed && std::chrono::steady_clock::now() >= deadline) {
            std::puts("diagnostic_deadline: terminating owned child");
            TerminateProcess(child.hProcess, 0xDEAD); killed = true;
        }
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) {
            if (WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0) break;
            continue;
        }
        DWORD continuation = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            auto& info = event.u.CreateProcessInfo;
            threads[event.dwThreadId] = info.hThread;
            const auto base = reinterpret_cast<DWORD64>(info.lpBaseOfImage);
            std::printf("module main base=%llx\n", base);
            SymLoadModuleExW(child.hProcess, info.hFile, executable.c_str(), nullptr, base, 0, nullptr, 0);
            if (info.hFile) CloseHandle(info.hFile);
            if (info.hProcess != child.hProcess) CloseHandle(info.hProcess);
        } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
            threads[event.dwThreadId] = event.u.CreateThread.hThread;
        } else if (event.dwDebugEventCode == EXIT_THREAD_DEBUG_EVENT) {
            if (auto it = threads.find(event.dwThreadId); it != threads.end()) {
                if (it->second != child.hThread) CloseHandle(it->second);
                threads.erase(it);
            }
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            auto& info = event.u.LoadDll;
            wchar_t path[32768]{};
            if (info.hFile && GetFinalPathNameByHandleW(info.hFile, path, 32768, FILE_NAME_NORMALIZED)) {
                const auto base = reinterpret_cast<DWORD64>(info.lpBaseOfDll);
                std::wprintf(L"module %ls base=%llx\n", path, base);
                SymLoadModuleExW(child.hProcess, info.hFile, path, nullptr, base, 0, nullptr, 0);
                auto name = std::filesystem::path(path).filename().wstring();
                std::transform(name.begin(), name.end(), name.begin(), towlower);
                if (name == L"ntdll.dll") {
                    const auto local = GetModuleHandleW(L"ntdll.dll");
                    for (const char* function : {"RtlExitUserProcess", "NtTerminateProcess"}) {
                        const auto address = base + reinterpret_cast<ULONG_PTR>(GetProcAddress(local, function))
                            - reinterpret_cast<ULONG_PTR>(local);
                        BYTE original{}, trap = 0xCC;
                        SIZE_T transferred{};
                        if (ReadProcessMemory(child.hProcess, reinterpret_cast<void*>(address), &original, 1, &transferred)
                            && WriteProcessMemory(child.hProcess, reinterpret_cast<void*>(address), &trap, 1, &transferred)) {
                            FlushInstructionCache(child.hProcess, reinterpret_cast<void*>(address), 1);
                            breakpoints[address] = {original, function};
                        }
                    }
                }
            }
            if (info.hFile) CloseHandle(info.hFile);
        } else if (event.dwDebugEventCode == UNLOAD_DLL_DEBUG_EVENT) {
            SymUnloadModule64(child.hProcess, reinterpret_cast<DWORD64>(event.u.UnloadDll.lpBaseOfDll));
        } else if (event.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT) {
            const auto& info = event.u.DebugString;
            const auto bytes = std::min<size_t>(info.nDebugStringLength, 8192) * (info.fUnicode ? 2 : 1);
            std::vector<char> message(bytes + 2, 0);
            if (ReadProcessMemory(child.hProcess, info.lpDebugStringData, message.data(), bytes, nullptr)) {
                if (info.fUnicode) std::wprintf(L"debug: %ls\n", reinterpret_cast<wchar_t*>(message.data()));
                else std::printf("debug: %s\n", message.data());
            }
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            auto& info = event.u.Exception;
            const auto address = reinterpret_cast<ULONG_PTR>(info.ExceptionRecord.ExceptionAddress);
            const auto thread = threads.find(event.dwThreadId);
            if (auto bp = breakpoints.find(address); bp != breakpoints.end()
                && info.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT && thread != threads.end()) {
                std::printf("exit_boundary=%s\n", bp->second.name.c_str());
                Stack(child.hProcess, thread->second);
                WriteProcessMemory(child.hProcess, reinterpret_cast<void*>(address), &bp->second.original, 1, nullptr);
                FlushInstructionCache(child.hProcess, reinterpret_cast<void*>(address), 1);
                CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
                GetThreadContext(thread->second, &context); context.Rip = address;
                SetThreadContext(thread->second, &context);
                breakpoints.erase(bp);
            } else if (info.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) {
                std::printf("exception=%08lx address=%llx first_chance=%lu\n",
                    info.ExceptionRecord.ExceptionCode, address, info.dwFirstChance);
                if (!info.dwFirstChance && thread != threads.end()) Stack(child.hProcess, thread->second);
                continuation = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            result = event.u.ExitProcess.dwExitCode;
            std::printf("exit_code=%lu hex=%08lx\n", result, result); alive = false;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation);
        std::fflush(stdout);
    }
    if (WaitForSingleObject(child.hProcess, 3000) != WAIT_OBJECT_0) {
        TerminateProcess(child.hProcess, 0xDEAD); WaitForSingleObject(child.hProcess, 3000);
    }
    SymCleanup(child.hProcess);
    for (auto [id, handle] : threads) if (handle != child.hThread) CloseHandle(handle);
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    std::printf("cleanup_complete=true\n");
    return result == 0 ? 0 : 1;
}
