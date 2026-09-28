#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <array>

namespace Horse::Deterministic {
// Logical UUMGSequencePlayer continuation, separate from its UObject, native
// evaluation root and callback/allocation ownership. Never install a player
// byte image. Native141826F90 consumes these clocks/loop/direction fields.
struct ReplayHudPlayback final {
    double time{}, end{}, offset{}, lower{}, upper{};
    float rate{};
    std::int32_t status{}, loops{}, completed_loops{}, mode{};
    std::uint8_t forward{}, lower_kind{}, upper_kind{};

    static bool Read(std::span<const std::byte> player, ReplayHudPlayback& output) noexcept {
        if (player.size() < 0x780) return false;
        ReplayHudPlayback value;
        const auto read = [&](auto& field, std::size_t offset) {
            std::memcpy(&field, player.data() + offset, sizeof(field));
        };
        read(value.time,0x6a0); read(value.end,0x6a8); read(value.offset,0x6b0);
        read(value.lower_kind,0x6b8); read(value.lower,0x6c0);
        read(value.upper_kind,0x6c8); read(value.upper,0x6d0);
        read(value.status,0x6d8); read(value.loops,0x750);
        read(value.completed_loops,0x754); read(value.rate,0x758);
        read(value.mode,0x75c); read(value.forward,0x760);
        output=value;
        return true;
    }

    // Scalar admission only, NOT permission to restore. The HUD transaction
    // must separately prove detached ownership, native callbacks, asset/track
    // inputs, evaluation retirement, B undo and memory admission. Restrict the
    // first reconstruction to the observed forward first-loop path; do not
    // coerce reverse/ping-pong or a completed player into that domain.
    bool forward_first_loop() const noexcept {
        return std::isfinite(time) && std::isfinite(end) && std::isfinite(offset)
            && std::isfinite(lower) && std::isfinite(upper) && std::isfinite(rate)
            && status==1 && loops==1 && completed_loops==0 && mode==0 && forward==1
            && lower_kind==1 && upper_kind==1 && lower==0 && end==0
            && upper>lower && time>=lower && time<=upper && rate>0;
    }
    bool matches_asset_range(float start,float finish) const noexcept {
        // Native1417F2F70 widens the authored float start;141801950 subtracts
        // float bounds before widening the duration. Preserve that arithmetic.
        return std::isfinite(start) && std::isfinite(finish)
            && offset==double(start) && upper==double(float(finish-start));
    }
    bool same_values(const ReplayHudPlayback& other) const noexcept {
        const auto same=[](const auto& a,const auto& b){return std::memcmp(&a,&b,sizeof(a))==0;};
        return same(time,other.time) && same(end,other.end) && same(offset,other.offset)
            && same(lower,other.lower) && same(upper,other.upper) && same(rate,other.rate)
            && status==other.status && loops==other.loops && completed_loops==other.completed_loops
            && mode==other.mode && forward==other.forward && lower_kind==other.lower_kind && upper_kind==other.upper_kind;
    }
};

// Journal for one retained native player. Native status=Stopped is not an
// ownership transition: evaluation can still require undo after completion.
class ReplayHudPlayerOperation final {
public:
    enum class Phase : std::uint8_t { Empty, Prepared, FinishingOriginal, OriginalFinished, Starting, Published, FinishingUndo, Retired, NativeOwned };
    Phase phase() const noexcept { return phase_; }
    bool Prepare() noexcept {
        if(phase_!=Phase::Empty)return false;
        phase_=Phase::Prepared;return true;
    }
    template<class Finish,class Start> bool Publish(Finish&& finish,Start&& start) {
        if(phase_!=Phase::Prepared)return false;
        phase_=Phase::FinishingOriginal;
        if(!finish())return false;
        phase_=Phase::OriginalFinished;
        phase_=Phase::Starting;
        if(!start())return false;
        phase_=Phase::Published;return true;
    }
    template<class Finish> bool Undo(Finish&& finish) {
        if(phase_==Phase::Empty || phase_==Phase::Retired)return true;
        if(phase_==Phase::NativeOwned)return false;
        if(phase_!=Phase::Prepared && phase_!=Phase::OriginalFinished) {
            phase_=Phase::FinishingUndo;
            if(!finish())return false;
        }
        phase_=Phase::Retired;return true;
    }
    bool Commit() noexcept {
        if(phase_==Phase::Empty || phase_==Phase::NativeOwned)return true;
        if(phase_!=Phase::Published)return false;
        phase_=Phase::NativeOwned;return true;
    }
private:
    Phase phase_{Phase::Empty};
};

// Private B is never reset or reconstructed. This witness is read-only and
// remains live until exact membership recovery or explicit commit retirement.
class ReplayHudPrivatePlayer final {
public:
    enum class Phase : std::uint8_t { Empty, Prepared, Private, Recovered, Retiring, Retired };
    Phase phase() const noexcept { return phase_; }
    bool Prepare(std::span<const std::byte> player) noexcept {
        if(phase_!=Phase::Empty || player.size()!=image_.size())return false;
        std::memcpy(image_.data(),player.data(),image_.size());
        phase_=Phase::Prepared;return true;
    }
    bool Unchanged(std::span<const std::byte> player) const noexcept {
        return (phase_==Phase::Prepared || phase_==Phase::Private || phase_==Phase::Recovered)
            && player.size()==image_.size() && !std::memcmp(player.data(),image_.data(),image_.size());
    }
    template<class Detach> bool Park(std::span<const std::byte> player,Detach&& detach) {
        if(phase_!=Phase::Prepared || !Unchanged(player))return false;
        phase_=Phase::Private; // journal before any fallible membership write
        return detach();
    }
    template<class Attach> bool Recover(std::span<const std::byte> player,Attach&& attach) {
        if(phase_==Phase::Empty)return true;
        if(!Unchanged(player))return false;
        if(phase_==Phase::Prepared || phase_==Phase::Recovered)return true;
        if(phase_!=Phase::Private || !attach())return false;
        phase_=Phase::Recovered;return true;
    }
    template<class Finish> bool Commit(std::span<const std::byte> player,Finish&& finish) {
        if(phase_==Phase::Empty || phase_==Phase::Retired)return true;
        if(phase_!=Phase::Retiring && (phase_!=Phase::Private || !Unchanged(player)))return false;
        phase_=Phase::Retiring;
        if(!finish())return false;
        phase_=Phase::Retired;return true;
    }
private:
    Phase phase_{Phase::Empty};
    std::array<std::byte,0x780> image_{};
};
}
