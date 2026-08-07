#include "../HorseMod/horselib/RollbackGameplayCrt.hpp"

#include <cstdio>

namespace
{
    int native_step(uint32_t& state) noexcept
    {
        state = Horse::AdvanceRollbackGameplayCrtState(state);
        return static_cast<int>(Horse::RollbackGameplayCrtOutput(state));
    }
}

int main()
{
    constexpr uint32_t seed = 0x12345673u;
    constexpr uint32_t thread = 77u;
    Horse::RollbackGameplayCrtBroker broker {};
    uint32_t native_state = seed >> 4;

    bool seed_ok = broker.begin_seed_transaction(seed, thread, false)
        && broker.observe_native_seed(seed >> 4, thread);
    for (uint32_t i = 0; seed_ok && i < (seed & 0xfffu); ++i)
    {
        const auto draw = broker.observe_native_warmup_draw(
            Horse::kRollbackCrtWarmupReturnRva,
            thread, native_step(native_state));
        seed_ok = draw.handled && !draw.fatal;
    }
    seed_ok = seed_ok && broker.finish_seed_transaction(thread)
        && Horse::RollbackGameplayCrtStateIsCanonical(broker.state())
        && broker.state().internal_state == native_state
        && broker.state().gameplay_draw_ordinal == 0;

    const auto movevm = broker.draw_owned(
        Horse::kRollbackCrtMoveVmReturnRva, thread);
    const auto saved = broker.state();
    const auto rannyu = broker.draw_owned(
        Horse::kRollbackCrtRannyuReturnRva, thread);
    const bool gameplay_routes = movevm.handled && !movevm.fatal
        && movevm.known_gameplay_caller
        && rannyu.handled && !rannyu.fatal
        && rannyu.known_gameplay_caller
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtParticleEmitterReturnRva)
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtAudioVoiceReturnRva)
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtGroundDebrisBaseYawReturnRva)
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtGroundDebrisYawJitterReturnRva)
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtParticleModuleSeedReturnRva)
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtEmitterDelayRangeReturnRva)
        && Horse::RollbackCrtCallerIsPresentation(
            Horse::kRollbackCrtEmitterDurationRangeReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtParticleEmitterReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtAudioVoiceReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtGroundDebrisBaseYawReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtGroundDebrisYawJitterReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtParticleModuleSeedReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtEmitterDelayRangeReturnRva)
        && !Horse::RollbackCrtCallerIsGameplay(
            Horse::kRollbackCrtEmitterDurationRangeReturnRva)
        && broker.state().gameplay_draw_ordinal == 2;

    Horse::RollbackGameplayCrtBroker mirrored {};
    uint32_t mirrored_native = seed >> 4;
    bool mirrored_ok = mirrored.begin_seed_transaction(seed, thread, false)
        && mirrored.observe_native_seed(seed >> 4, thread);
    for (uint32_t i = 0; mirrored_ok && i < (seed & 0xfffu); ++i)
    {
        mirrored_ok = !mirrored.observe_native_warmup_draw(
            Horse::kRollbackCrtWarmupReturnRva, thread,
            native_step(mirrored_native)).fatal;
    }
    mirrored_ok = mirrored_ok && mirrored.finish_seed_transaction(thread);
    const int mirrored_native_value = native_step(mirrored_native);
    const auto mirrored_draw = mirrored.observe_native_gameplay_draw(
        Horse::kRollbackCrtMoveVmReturnRva, thread,
        mirrored_native_value);
    mirrored_ok = mirrored_ok && !mirrored_draw.fatal
        && mirrored_draw.value == mirrored_native_value
        && mirrored.state().internal_state == mirrored_native;

    const bool restore_ok = broker.restore(saved)
        && broker.state().gameplay_draw_ordinal == 1
        && broker.draw_owned(
            Horse::kRollbackCrtRannyuReturnRva, thread).value
            == rannyu.value;

    Horse::RollbackGameplayCrtBroker unknown {};
    bool unknown_seed = unknown.begin_seed_transaction(0x10u, thread, false)
        && unknown.observe_native_seed(1u, thread);
    uint32_t unknown_native = 1u;
    for (uint32_t i = 0; unknown_seed && i < 0x10u; ++i)
    {
        unknown_seed = !unknown.observe_native_warmup_draw(
            Horse::kRollbackCrtWarmupReturnRva, thread,
            native_step(unknown_native)).fatal;
    }
    unknown_seed = unknown_seed && unknown.finish_seed_transaction(thread);
    const auto unknown_draw = unknown.draw_owned(0xDEADBEEFu, thread);
    const bool unknown_fail_closed = unknown_seed
        && unknown_draw.handled && unknown_draw.fatal
        && !unknown_draw.known_gameplay_caller
        && unknown_draw.value >= 0 && unknown_draw.value <= 0x7fff
        && unknown.state().gameplay_draw_ordinal == 1;

    Horse::RollbackGameplayCrtBroker negative {};
    const bool pending_debt_rejected =
        !negative.begin_seed_transaction(seed, thread, true);
    negative.reset();
    const bool truncated_warmup_rejected =
        negative.begin_seed_transaction(seed, thread, false)
        && negative.observe_native_seed(seed >> 4, thread)
        && !negative.finish_seed_transaction(thread);
    negative.reset();
    const bool wrong_native_seed_rejected =
        negative.begin_seed_transaction(seed, thread, false)
        && !negative.observe_native_seed((seed >> 4) + 1u, thread);

    const bool ok = seed_ok && gameplay_routes && mirrored_ok && restore_ok
        && unknown_fail_closed && pending_debt_rejected
        && truncated_warmup_rejected && wrong_native_seed_rejected;
    std::printf(
        "rollback gameplay CRT self-test %s seed=%d routes=%d restore=%d "
        "mirror=%d unknown=%d debt=%d truncated=%d wrong_seed=%d\n",
        ok ? "passed" : "failed", seed_ok ? 1 : 0,
        gameplay_routes ? 1 : 0, restore_ok ? 1 : 0,
        mirrored_ok ? 1 : 0,
        unknown_fail_closed ? 1 : 0, pending_debt_rejected ? 1 : 0,
        truncated_warmup_rejected ? 1 : 0,
        wrong_native_seed_rejected ? 1 : 0);
    return ok ? 0 : 1;
}
