#pragma once
#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>

// Bounded, process-lifetime diagnostic of native fatal errors. The observer
// module and these three hooks stay pinned until the owned process exits: there
// is no concurrent-error/unhook race and no claim of native task cancellation.
class ReplayStartupFailure final {
public:
    static bool Start(std::uintptr_t base,std::string_view run,const std::filesystem::path& root) {
        if(owner_ || !base || run.empty() || run.size()>96)return false;
        auto state=std::make_unique<ReplayStartupFailure>();
        state->base_=base;
        const auto path=(root/L"consumer_startup_failure.json").wstring();
        const auto temporary=(root/L"consumer_startup_failure.tmp").wstring();
        if(path.size()>=state->path_.size() || temporary.size()>=state->temporary_.size())return false;
        std::memcpy(state->path_.data(),path.c_str(),(path.size()+1)*sizeof(wchar_t));
        std::memcpy(state->temporary_.data(),temporary.c_str(),(temporary.size()+1)*sizeof(wchar_t));
        std::memcpy(state->run_.data(),run.data(),run.size());
        // Signature populated from the retained native fatal-handler evidence.
        if(!Signature(base))return false;
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Entry),&module))return false;
        state->observer_=reinterpret_cast<std::uintptr_t>(module);
        state->ucrt_=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"ucrtbase.dll"));
        state->framework_=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"UE4SS.dll"));
        state->hook_=std::make_unique<PLH::x64Detour>(base+0xe0ed30,
            reinterpret_cast<std::uint64_t>(&Entry),&state->original_);
        state->exit_hook_=std::make_unique<PLH::x64Detour>(base+0xe0e590,
            reinterpret_cast<std::uint64_t>(&ExitEntry),&state->original_exit_);
        state->exception_hook_=std::make_unique<PLH::x64Detour>(base+0xe0d330,
            reinterpret_cast<std::uint64_t>(&ExceptionEntry),&state->original_exception_);
        owner_=state.get();
        if(!state->hook_->hook()){owner_=nullptr;return false;}
        // Once any entry is published, retain its owner even if the second
        // installation fails. Never unhook/free under a concurrent fatal call.
        if(!state->exit_hook_->hook()){(void)state.release();return false;}
        if(!state->exception_hook_->hook()){(void)state.release();return false;}
        (void)state.release(); // Fixed bounded process owner; reclaimed by OS at exit.
        return true;
    }
private:
    static bool Signature(std::uintptr_t base) {
        static constexpr unsigned char expected[]{0x40,0x53,0x48,0x83,0xec,0x20,0x80,0x3d,0xf8,0x83,0x38,0x03,0x00,0x48,0x8b,0xd9,0x75,0x0d,0xff,0x15,0x78,0xd7,0x41,0x02,0xc6,0x05,0xe6,0x83,0x38,0x03,0x01,0x80};
        static constexpr unsigned char exit_expected[]{0x40,0x53,0x48,0x83,0xec,0x20,0x0f,0xb6,0xd9,0x48,0x8d,0x0d,0xa0,0x7d,0x26,0x03,0xe8,0x1b,0xb4,0x58,0xff,0x84,0xdb,0x74,0x4b,0xe8,0x12,0xb6,0xfb,0xff,0x48,0x85};
        static constexpr unsigned char exception_expected[]{0x48,0x83,0xec,0x28,0x48,0x83,0x3d,0x34,0xb3,0x48,0x03,0x00,0x48,0x8b,0xc1,0x74,0x3f,0xba,0x01,0x00,0x00,0x00,0xf0,0x0f,0xc1,0x15,0x12,0xb1,0x38,0x03,0xff,0xc2};
        return !std::memcmp(reinterpret_cast<void*>(base+0xe0ed30),expected,sizeof(expected))
            && !std::memcmp(reinterpret_cast<void*>(base+0xe0e590),exit_expected,sizeof(exit_expected))
            && !std::memcmp(reinterpret_cast<void*>(base+0xe0d330),exception_expected,sizeof(exception_expected));
    }
    static std::size_t ErrorText(const wchar_t* source,wchar_t* text,std::size_t capacity) noexcept {
        if(!source)return 0;
        __try {
            std::size_t n{};
            while(n+1<capacity && source[n]){text[n]=source[n];++n;}
            return n;
        } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    }
    static void Entry(void* device,const wchar_t* text,unsigned char verbosity,std::uintptr_t category) {
        auto* self=owner_.load(std::memory_order_acquire);
        self->Record(text);
        reinterpret_cast<void(*)(void*,const wchar_t*,unsigned char,std::uintptr_t)>(self->original_)(device,text,verbosity,category);
    }
    static void ExitEntry(bool force) {
        auto* self=owner_.load(std::memory_order_acquire);
        if(force)self->Record(reinterpret_cast<const wchar_t*>(self->base_+0x418b130),
            "140E0E590","native_history_may_be_stale");
        reinterpret_cast<void(*)(bool)>(self->original_exit_)(force);
    }
    static std::uint32_t ExceptionEntry(EXCEPTION_POINTERS* exception) {
        auto* self=owner_.load(std::memory_order_acquire);
        self->Record(nullptr,"140E0D330","native_exception_context",exception);
        return reinterpret_cast<std::uint32_t(*)(EXCEPTION_POINTERS*)>(self->original_exception_)(exception);
    }
    struct ExceptionSnapshot {
        bool valid{};
        std::uint64_t code{},flags{},address{},rip{},rsp{},rax{},rbx{},rcx{},rdx{},rsi{},rdi{},rbp{};
        std::uint64_t r8{},r9{},r10{},r11{},r12{},r13{},r14{},r15{};
        std::array<std::uint64_t,EXCEPTION_MAXIMUM_PARAMETERS> parameters{};
        unsigned count{};
    };
    static ExceptionSnapshot Snapshot(EXCEPTION_POINTERS* exception) noexcept {
        ExceptionSnapshot result{};
        __try {
            if(!exception || !exception->ExceptionRecord || !exception->ContextRecord)return result;
            const auto& record=*exception->ExceptionRecord;const auto& context=*exception->ContextRecord;
            result.code=record.ExceptionCode;result.flags=record.ExceptionFlags;
            result.address=reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
            result.count=record.NumberParameters<EXCEPTION_MAXIMUM_PARAMETERS?record.NumberParameters:EXCEPTION_MAXIMUM_PARAMETERS;
            for(unsigned i=0;i<result.count;++i)result.parameters[i]=record.ExceptionInformation[i];
            result.rip=context.Rip;result.rsp=context.Rsp;result.rax=context.Rax;result.rbx=context.Rbx;
            result.rcx=context.Rcx;result.rdx=context.Rdx;result.rsi=context.Rsi;result.rdi=context.Rdi;result.rbp=context.Rbp;
            result.r8=context.R8;result.r9=context.R9;result.r10=context.R10;result.r11=context.R11;
            result.r12=context.R12;result.r13=context.R13;result.r14=context.R14;result.r15=context.R15;
            result.valid=true;
        } __except(EXCEPTION_EXECUTE_HANDLER){result={};}
        return result;
    }
    int FatalFlag() const noexcept {
        __try {return *reinterpret_cast<const unsigned char*>(base_+0x4197135);}
        __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
    }
    void Record(const wchar_t* text,const char* site="140E0ED30",const char* source="incoming_serialize",EXCEPTION_POINTERS* exception=nullptr) noexcept {
        if(InterlockedCompareExchange(&recorded_,1,0))return;
        void* frames[32]{};
        const auto count=CaptureStackBackTrace(0,32,frames,nullptr);
        wchar_t error[768]{};
        const auto chars=ErrorText(text,error,std::size(error));
        const auto snapshot=Snapshot(exception);
        char output[8192]{};std::size_t size{};
        const auto append=[&](const char* text){while(*text && size+1<sizeof(output))output[size++]=*text++;};
        const auto hex=[&](std::uint64_t value){
            append("\"0x");
            for(int shift=60;shift>=0;shift-=4)output[size++]="0123456789abcdef"[(value>>shift)&15];
            append("\"");
        };
        append("{\"run_id\":\"");append(run_.data());append("\",\"site\":\"");append(site);append("\",\"game_base\":");hex(base_);
        append(",\"error_source\":\"");append(source);append("\",\"fatal_flag\":");
        const auto fatal=FatalFlag();append(fatal<0?"null":fatal?"true":"false");
        append(",\"observer_base\":");hex(observer_);append(",\"framework_base\":");hex(framework_);
        append(",\"ucrt_base\":");hex(ucrt_);append(",\"thread\":");hex(GetCurrentThreadId());
        append(",\"exception\":{\"valid\":");append(snapshot.valid?"true":"false");
        if(snapshot.valid) {
            const auto field=[&](const char* name,std::uint64_t value){append(",\"");append(name);append("\":");hex(value);};
            field("code",snapshot.code);field("flags",snapshot.flags);field("address",snapshot.address);
            field("rip",snapshot.rip);field("rsp",snapshot.rsp);field("rax",snapshot.rax);field("rbx",snapshot.rbx);
            field("rcx",snapshot.rcx);field("rdx",snapshot.rdx);field("rsi",snapshot.rsi);field("rdi",snapshot.rdi);field("rbp",snapshot.rbp);
            field("r8",snapshot.r8);field("r9",snapshot.r9);field("r10",snapshot.r10);field("r11",snapshot.r11);
            field("r12",snapshot.r12);field("r13",snapshot.r13);field("r14",snapshot.r14);field("r15",snapshot.r15);
            append(",\"parameters\":[");
            for(unsigned i=0;i<snapshot.count;++i){if(i)append(",");hex(snapshot.parameters[i]);}
            append("]");
        }
        append("}");
        append(",\"error\":\"");
        for(std::size_t i=0;i<chars;++i) {
            append("\\u");
            for(int shift=12;shift>=0;shift-=4)output[size++]="0123456789abcdef"[(error[i]>>shift)&15];
        }
        append("\",\"error_at_capacity\":");append(chars+1==std::size(error)?"true":"false");append(",\"stack\":[");
        for(USHORT i=0;i<count;++i){if(i)append(",");hex(reinterpret_cast<std::uintptr_t>(frames[i]));}
        append("]}\n");
        const auto file=CreateFileW(temporary_.data(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
            CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        DWORD written{};
        const bool complete=WriteFile(file,output,static_cast<DWORD>(size),&written,nullptr) && written==size;
        FlushFileBuffers(file);CloseHandle(file);
        if(complete)MoveFileExW(temporary_.data(),path_.data(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    inline static std::atomic<ReplayStartupFailure*> owner_{};
    std::array<wchar_t,1024> path_{},temporary_{};
    std::array<char,97> run_{};
    std::uintptr_t base_{},observer_{},framework_{},ucrt_{};
    LONG recorded_{};
    std::uint64_t original_{},original_exit_{},original_exception_{};
    std::unique_ptr<PLH::x64Detour> hook_;
    std::unique_ptr<PLH::x64Detour> exit_hook_;
    std::unique_ptr<PLH::x64Detour> exception_hook_;
};
