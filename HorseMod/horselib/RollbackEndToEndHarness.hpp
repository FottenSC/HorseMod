// ============================================================================
// Horse::RollbackEndToEndHarness
//
// Two actual Gekko game sessions joined through Horse's custom adapter. This
// acceptance test is event-driven: peer-delivered inputs must cause real
// Gekko Save, Load and Advance events, including a rollback Advance, and both
// independently simulated peers must converge.
// ============================================================================

#pragma once

#include "RollbackGekkoAdapter.hpp"

#include <cstdint>

namespace Horse
{
    struct RollbackEndToEndSelfTestReport
    {
        bool ok {false};
        bool decoded_payloads {false};
        bool bridge_roundtrip {false};
        bool prediction_written {false};
        bool prediction_diverged {false};
        bool adapter_receive_exercised {false};
        bool adapter_free_exercised {false};
        bool metadata_accepted {false};
        bool metadata_requires_correction {false};
        bool metadata_not_gameplay_input {false};
        bool confirmed_applied {false};
        bool confirmed_consumed {false};
        bool initial_baseline_event_order {false};
        bool state_converged {false};
        bool wrong_identity_rejected {false};
        uint32_t enqueued_packets {0};
        uint32_t drained_packets {0};
        uint32_t rejected_packets {0};
        uint32_t cache_write_sequence {0};
        uint32_t predicted_checksum_a {0};
        uint32_t predicted_checksum_b {0};
        uint32_t confirmed_checksum_a {0};
        uint32_t confirmed_checksum_b {0};
        uint32_t save_events {0};
        uint32_t load_events {0};
        uint32_t advance_events {0};
        uint32_t rollback_advance_events {0};
        const char* failure {"not-run"};
    };

    static inline RollbackEndToEndSelfTestReport
    RunRollbackEndToEndSelfTest() noexcept
    {
        RollbackEndToEndSelfTestReport report {};
        const RollbackGekkoAdapterSelfTestReport actual =
            RunRollbackGekkoAdapterSelfTest();

        report.decoded_payloads = actual.gameplay_inputs_decoded;
        report.bridge_roundtrip = actual.bridge_roundtrip
            && actual.bidirectional_payloads;
        report.prediction_written = actual.saw_save;
        report.prediction_diverged = actual.saw_rollback_advance;
        report.adapter_receive_exercised = actual.callbacks_received;
        report.adapter_free_exercised = actual.callbacks_freed;
        report.metadata_accepted = actual.bridge_metadata_accepted;
        report.metadata_requires_correction = actual.saw_load;
        report.metadata_not_gameplay_input = actual.bridge_rejections_ok;
        report.confirmed_applied = actual.saw_advance;
        report.confirmed_consumed = actual.gameplay_slots_present;
        report.initial_baseline_event_order =
            actual.initial_baseline_event_order_a
            && actual.initial_baseline_event_order_b;
        report.state_converged = actual.final_checksums_match;
        report.wrong_identity_rejected = actual.bridge_rejections_ok;
        report.enqueued_packets = actual.packets_sent;
        report.drained_packets = actual.packets_received;
        report.rejected_packets = actual.bridge_packets_rejected;
        report.cache_write_sequence = actual.gameplay_decoded_inputs;
        report.confirmed_checksum_a = actual.final_checksum_a;
        report.confirmed_checksum_b = actual.final_checksum_b;
        report.save_events = actual.save_events;
        report.load_events = actual.load_events;
        report.advance_events = actual.advance_events;
        report.rollback_advance_events = actual.rollback_advance_events;
        report.failure = actual.failure;
        report.ok = actual.ok
            && report.save_events > 0
            && report.load_events > 0
            && report.advance_events > 0
            && report.rollback_advance_events > 0
            && report.initial_baseline_event_order
            && report.state_converged;
        if (report.ok) report.failure = "ok";
        return report;
    }
}
