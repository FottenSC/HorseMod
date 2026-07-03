#include "../HorseMod/horselib/RollbackFaultInject.hpp"

#include <cstring>
#include <cstdio>

namespace
{
    void print_channel(
        const char* label,
        const Horse::RollbackFaultInjectionStats& stats)
    {
        std::printf(
            "%s submitted=%u queued=%u delivered=%u dropped=%u dup=%u "
            "reordered=%u corrupted=%u rejected=%u resends=%u "
            "overflow=%u max_queue=%u max_latency=%u spike=%d burst=%d ",
            label,
            stats.packets_submitted,
            stats.packets_queued,
            stats.packets_delivered,
            stats.packets_dropped,
            stats.packets_duplicated,
            stats.packets_reordered,
            stats.packets_corrupted,
            stats.packets_rejected,
            stats.resend_packets,
            stats.queue_overflow,
            stats.max_queue_depth,
            stats.max_latency_frames,
            stats.spike_applied ? 1 : 0,
            stats.burst_applied ? 1 : 0);
    }

    bool parse_profile(
        const char* text,
        Horse::RollbackNetworkProfileKind& out)
    {
        if (!text)
            return false;
        struct NameMap
        {
            const char* name;
            Horse::RollbackNetworkProfileKind kind;
        };
        const NameMap names[] = {
            {"clean_0ms", Horse::RollbackNetworkProfileKind::Clean0ms},
            {"wifi_50ms_jitter",
             Horse::RollbackNetworkProfileKind::Wifi50msJitter},
            {"bad_wifi_120ms_5pct_loss",
             Horse::RollbackNetworkProfileKind::BadWifi120ms5PctLoss},
            {"overseas_180ms_2pct_loss",
             Horse::RollbackNetworkProfileKind::Overseas180ms2PctLoss},
            {"spike_every_10s",
             Horse::RollbackNetworkProfileKind::SpikeEvery10s},
            {"burst_loss_500ms",
             Horse::RollbackNetworkProfileKind::BurstLoss500ms},
            {"corrupt_probe",
             Horse::RollbackNetworkProfileKind::CorruptProbe},
        };
        for (const NameMap& entry : names)
        {
            if (std::strcmp(text, entry.name) == 0)
            {
                out = entry.kind;
                return true;
            }
        }
        return false;
    }

    void print_profiles()
    {
        std::printf(
            "clean_0ms\n"
            "wifi_50ms_jitter\n"
            "bad_wifi_120ms_5pct_loss\n"
            "overseas_180ms_2pct_loss\n"
            "spike_every_10s\n"
            "burst_loss_500ms\n"
            "corrupt_probe\n");
    }

    int run_single_profile(Horse::RollbackNetworkProfileKind kind)
    {
        const Horse::RollbackNetworkProfile profile =
            Horse::GetRollbackNetworkProfile(kind);
        const Horse::RollbackFaultProfileRunReport run =
            Horse::RunRollbackFaultProfileSimulation(profile);
        std::printf(
            "rollback fault profile %s %s failure=%s ticks=%u frames=%u "
            "converged=%d exercised=%d recovered=%d conflicts_ok=%d "
            "window_ok=%d a_contig=%u b_contig=%u a_ack=%u b_ack=%u "
            "accepted_a=%u accepted_b=%u payload_match=%d "
            "first_faults_ab=%u first_faults_ba=%u "
            "resend_recovered_ab=%u resend_recovered_ba=%u "
            "expected=0x%08X checksum_a=0x%08X checksum_b=0x%08X ",
            profile.name,
            run.ok ? "passed" : "failed",
            run.failure ? run.failure : "?",
            run.ticks,
            run.frame_count,
            run.both_peers_converged ? 1 : 0,
            run.fault_profile_exercised ? 1 : 0,
            run.ack_resend_recovered ? 1 : 0,
            run.no_conflicts ? 1 : 0,
            run.no_over_window_late ? 1 : 0,
            run.peer_a_contiguous,
            run.peer_b_contiguous,
            run.peer_a_ack_of_local,
            run.peer_b_ack_of_local,
            run.peer_a_unique_accepted,
            run.peer_b_unique_accepted,
            run.accepted_payloads_match ? 1 : 0,
            run.first_send_faults_a_to_b,
            run.first_send_faults_b_to_a,
            run.recovered_by_resend_a_to_b,
            run.recovered_by_resend_b_to_a,
            run.expected_checksum,
            run.checksum_a,
            run.checksum_b);
        print_channel("a_to_b", run.a_to_b);
        print_channel("b_to_a", run.b_to_a);
        std::printf("\n");
        return run.ok ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--list-profiles") == 0)
        {
            print_profiles();
            return 0;
        }
        if (std::strcmp(argv[i], "--profile") == 0 && i + 1 < argc)
        {
            Horse::RollbackNetworkProfileKind kind {};
            if (!parse_profile(argv[i + 1], kind))
            {
                std::printf("unknown rollback fault profile: %s\n", argv[i + 1]);
                print_profiles();
                return 2;
            }
            return run_single_profile(kind);
        }
    }

    const Horse::RollbackFaultInjectSelfTestReport report =
        Horse::RunRollbackFaultInjectSelfTest();
    if (!report.ok)
    {
        const Horse::RollbackFaultProfileRunReport& fail =
            report.last_failure;
        std::printf(
            "rollback fault-injection self-test failed failure=%s "
            "profiles=%u/%u clean=%d wifi=%d bad_wifi=%d overseas=%d "
            "spike=%d burst=%d corrupt=%d converged=%d "
            "failed_profile=%s failed_reason=%s ticks=%u frames=%u "
            "a_contig=%u b_contig=%u a_ack=%u b_ack=%u "
            "accepted_a=%u accepted_b=%u payload_match=%d "
            "first_faults_ab=%u first_faults_ba=%u "
            "resend_recovered_ab=%u resend_recovered_ba=%u "
            "expected=0x%08X checksum_a=0x%08X checksum_b=0x%08X ",
            report.failure ? report.failure : "?",
            report.profiles_passed,
            report.profiles_run,
            report.clean_profile_ok ? 1 : 0,
            report.wifi_jitter_profile_ok ? 1 : 0,
            report.bad_wifi_profile_ok ? 1 : 0,
            report.overseas_profile_ok ? 1 : 0,
            report.spike_profile_ok ? 1 : 0,
            report.burst_profile_ok ? 1 : 0,
            report.corrupt_probe_ok ? 1 : 0,
            report.same_machine_profiles_converged ? 1 : 0,
            fail.profile_name ? fail.profile_name : "?",
            fail.failure ? fail.failure : "?",
            fail.ticks,
            fail.frame_count,
            fail.peer_a_contiguous,
            fail.peer_b_contiguous,
            fail.peer_a_ack_of_local,
            fail.peer_b_ack_of_local,
            fail.peer_a_unique_accepted,
            fail.peer_b_unique_accepted,
            fail.accepted_payloads_match ? 1 : 0,
            fail.first_send_faults_a_to_b,
            fail.first_send_faults_b_to_a,
            fail.recovered_by_resend_a_to_b,
            fail.recovered_by_resend_b_to_a,
            fail.expected_checksum,
            fail.checksum_a,
            fail.checksum_b);
        print_channel("a_to_b", fail.a_to_b);
        print_channel("b_to_a", fail.b_to_a);
        std::printf("\n");
        return 1;
    }

    std::printf(
        "rollback fault-injection self-test passed profiles=%u/%u "
        "clean=%d wifi=%d bad_wifi=%d overseas=%d spike=%d burst=%d "
        "corrupt=%d converged=%d\n",
        report.profiles_passed,
        report.profiles_run,
        report.clean_profile_ok ? 1 : 0,
        report.wifi_jitter_profile_ok ? 1 : 0,
        report.bad_wifi_profile_ok ? 1 : 0,
        report.overseas_profile_ok ? 1 : 0,
        report.spike_profile_ok ? 1 : 0,
        report.burst_profile_ok ? 1 : 0,
        report.corrupt_probe_ok ? 1 : 0,
        report.same_machine_profiles_converged ? 1 : 0);
    return 0;
}
