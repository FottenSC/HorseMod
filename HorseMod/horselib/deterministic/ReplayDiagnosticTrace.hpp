#pragma once

#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <map>

namespace Horse::Deterministic {
// Read-only diagnostic admission shared by the existing hook owners. No new
// native hooks, retained native pointers, or dynamically growing event buffer.
class ReplayDiagnosticTrace final {
public:
    enum Subsystem : unsigned { Audio=1, Rng=2, Particles=4, Presentation=8 };
    // Two DLL instances plus bounded stack/formatting scratch are charged by
    // the observer's reservation. Output bytes have a separate per-DLL ceiling.
    static constexpr std::size_t reservation_bytes = 8192;
    unsigned stack_depth{}, mask{}, first_tick{}, last_tick{36000}, byte_limit{1048576};
    bool valid{true};

    bool Load(const std::filesystem::path& path) {
        stack_depth=mask=first_tick=0;last_tick=36000;byte_limit=1048576;
        used_.store(0);overflow_.store(false);valid=true;
        if (!std::filesystem::exists(path)) return true; // ordinary play: disabled
        std::ifstream stream(path);
        std::map<std::string,unsigned> fields;
        std::string line;
        while(std::getline(stream,line)) {
            if(!line.empty() && line.back()=='\r')line.pop_back();
            const auto separator=line.find('=');
            if(separator==std::string::npos) {valid=false;break;}
            unsigned value{};
            const auto* begin=line.data()+separator+1;
            const auto* end=line.data()+line.size();
            const auto parsed=std::from_chars(begin,end,value);
            if(parsed.ec!=std::errc{} || parsed.ptr!=end || !fields.emplace(line.substr(0,separator),value).second) {valid=false;break;}
        }
        valid=valid && stream.eof() && fields.size()==5
            && fields.contains("stack_depth") && fields.contains("mask")
            && fields.contains("first_tick") && fields.contains("last_tick") && fields.contains("byte_limit");
        if(valid) {
            stack_depth=fields["stack_depth"];mask=fields["mask"];first_tick=fields["first_tick"];
            last_tick=fields["last_tick"];byte_limit=fields["byte_limit"];
            valid=(stack_depth==0 || stack_depth==8 || stack_depth==16) && mask<=15
                && first_tick<=last_tick && last_tick<=36000 && byte_limit>=4096 && byte_limit<=16*1024*1024;
        }
        if(!valid)mask=0;
        return valid;
    }
    bool LoadQualification() {
        const auto* local=std::getenv("LOCALAPPDATA");
        return local ? Load(std::filesystem::path(local)/"HorseMod/Qualification/replay_diagnostics.ini") : true;
    }
    // 1=admitted, 0=disabled/dropped, -1=first overflow. Reserve a conservative
    // upper bound for all text emitted by one event, not the native payload size.
    int Admit(unsigned subsystem,unsigned tick,unsigned bytes=2048) noexcept {
        if(!(mask&subsystem) || tick<first_tick || tick>last_tick)return 0;
        auto used=used_.load();
        do {
            if(bytes>byte_limit || used>byte_limit-bytes)
                return overflow_.exchange(true) ? 0 : -1;
        } while(!used_.compare_exchange_weak(used,used+bytes));
        return 1;
    }
    bool Overflowed() const noexcept {return overflow_.load();}
private:
    std::atomic<unsigned> used_{};
    std::atomic<bool> overflow_{};
};
inline ReplayDiagnosticTrace g_replay_diagnostics;
}
