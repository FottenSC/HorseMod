#include "../HorseMod/horselib/RollbackPerformanceTelemetry.hpp"
#include "../HorseMod/horselib/RollbackRuntimePolicy.hpp"

#include <cstdio>

int main()
{
    Horse::RollbackLeadPacingConfig config {};
    Horse::RollbackLeadPacingController pacing {};
    const bool below_enter = pacing.decide(config, 1.499f, true)
        == Horse::RollbackLeadPacingDecision::Advance;
    const bool first_hold = pacing.decide(config, 1.5f, true)
        == Horse::RollbackLeadPacingDecision::HoldAndFlushCorrections
        && pacing.consecutive_holds() == 1;
    const bool second_hold = pacing.decide(config, 1.0f, true)
        == Horse::RollbackLeadPacingDecision::HoldAndFlushCorrections
        && pacing.consecutive_holds() == 2;
    const bool mandatory_advance = pacing.decide(config, 2.0f, true)
        == Horse::RollbackLeadPacingDecision::Advance
        && pacing.forced_advance_last_decision();
    const bool resumes_holding = pacing.decide(config, 2.0f, true)
        == Horse::RollbackLeadPacingDecision::HoldAndFlushCorrections;
    const bool exits = pacing.decide(config, 0.5f, true)
        == Horse::RollbackLeadPacingDecision::Advance
        && !pacing.holding();
    const bool ineligible_resets =
        pacing.decide(config, 8.0f, false)
            == Horse::RollbackLeadPacingDecision::Advance
        && !pacing.holding();

    Horse::RollbackPerformanceTelemetry telemetry {};
    telemetry.observe_duration(telemetry.owned_tick, 250000);
    telemetry.observe_duration(telemetry.owned_tick, 250001);
    telemetry.observe_duration(telemetry.owned_tick, 50000001);
    telemetry.rollback_depth.observe(0);
    telemetry.rollback_depth.observe(60);
    telemetry.rollback_depth.observe(61);
    telemetry.frame_lead.observe(-8.5f);
    telemetry.frame_lead.observe(0.0f);
    telemetry.frame_lead.observe(8.5f);
    telemetry.observe_effect_lag(0, 7, 0);
    telemetry.observe_effect_lag(0, 3, 0);
    telemetry.observe_effect_lag(
        Horse::RollbackPerformanceTelemetry::kSideEffectTypeCount,
        99, 99);
    const bool histograms = telemetry.owned_tick.conserved()
        && telemetry.owned_tick.samples == 3
        && telemetry.owned_tick.buckets[0] == 1
        && telemetry.owned_tick.buckets[1] == 1
        && telemetry.owned_tick.buckets.back() == 1
        && telemetry.rollback_depth.conserved()
        && telemetry.rollback_depth.buckets[0] == 1
        && telemetry.rollback_depth.buckets[60] == 1
        && telemetry.rollback_depth.buckets.back() == 1
        && telemetry.frame_lead.conserved()
        && telemetry.frame_lead.buckets.front() == 1
        && telemetry.frame_lead.buckets[17] == 1
        && telemetry.frame_lead.buckets.back() == 1
        && telemetry.effect_produced_to_eligible_samples[0] == 2
        && telemetry.effect_produced_to_eligible_total[0] == 10
        && telemetry.effect_produced_to_eligible_maximum[0] == 7
        && telemetry.effect_eligible_to_committed_samples[0] == 2
        && telemetry.effect_eligible_to_committed_total[0] == 0;

    const bool ok = config.valid() && below_enter && first_hold
        && second_hold && mandatory_advance && resumes_holding && exits
        && ineligible_resets && histograms;
    std::printf("rollback runtime policy self-test %s\n",
        ok ? "passed" : "failed");
    return ok ? 0 : 1;
}
