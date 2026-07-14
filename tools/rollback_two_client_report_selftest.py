#!/usr/bin/env python3
"""Focused self-tests for two-client navigation report normalization."""

from __future__ import annotations

import unittest

import rollback_two_client_test_run as report
from rollback_report_contract import contract_fields, coverage, validate_v2


def main_menu_stage(**overrides: object) -> dict[str, object]:
    stage: dict[str, object] = {
        "ts_qpc": 100,
        "request_id": "nav-request",
        "current_scene_class": "MainMenuScene_C",
        "current_scene_name": "MainMenuScene_C_0",
        "main_menu_navigation_state": "awaiting-acknowledgement",
        "main_menu_navigation_failure": "none",
        "main_menu_input_sequence_complete": False,
        "main_menu_input_attempts": 1,
        "main_menu_input_last_reason": "waiting for semantic evidence",
        "main_menu_input_last_ok": False,
        "player_match_scene_request_attempts": 1,
        "online_nav_attempts": 10,
    }
    stage.update(overrides)
    return stage


def semantic_event(**overrides: object) -> dict[str, object]:
    event: dict[str, object] = {
        "ts_qpc": 101,
        "request_id": "nav-request",
        "dispatch_accepted": True,
        "call_success": True,
        "semantic_action_started": False,
        "target_scene_queued": False,
        "focus_before_live_dispatchable": True,
        "focus_after_live_dispatchable": False,
        "selection_changed": False,
        "scene_transitioned": False,
        "action_acknowledged": False,
        "navigation_state_name": "awaiting-acknowledgement",
        "navigation_failure": "none",
        "navigation_generation": 7,
        "navigation_dispatch_id": 3,
        "navigation_step_attempts": 1,
        "navigation_total_dispatches": 3,
        "sequence_complete": False,
        "reason": "semantic Blueprint dispatch accepted",
        "ok": True,
        "key_name": "Decide",
    }
    event.update(overrides)
    return event


def production_status(**overrides: object) -> dict[str, object]:
    status: dict[str, object] = {
        "lifecycle_mode": "mirrored-versus",
        "state": report.ROLLBACK_PRODUCTION_ACTIVE_STATE,
        "failure": "ok",
        "executable_match": True,
        "schema_match": True,
        "manifest_ready": True,
        "lifecycle_ready": True,
        "peer_ready": True,
        "native_input_source_slot": 0,
        "network_profile": "clean_0ms",
        "fault_seed": 0x5C6B0001,
        "fault_submitted": 8,
        "fault_delivered": 8,
        "fault_queued": 0,
        "fault_reordered": 0,
        "fault_dropped": 0,
        "fault_corrupted": 0,
        "fault_spiked": 0,
        "fault_burst_dropped": 0,
        "gekko_slot": 0,
        "tick_hook_installed": True,
        "presentation_hooks_installed": True,
        "desired_descriptor_hash": 0x1234,
        "observed_descriptor_hash": 0x1234,
        "peer_descriptor_hash": 0x1234,
        "setup_barrier_local": True,
        "setup_barrier_peer": True,
        "baseline_barrier_local": True,
        "baseline_barrier_peer": True,
        "baseline_frame": 10,
        "baseline_epoch": 0xA0,
        "peer_baseline_epoch": 0xA0,
        "baseline_hash": 0xB0,
        "peer_baseline_hash": 0xB0,
        "baseline_restore_verified": True,
        "prediction_restore_verified": True,
        "final_restore_verified": True,
        "presentation_exactly_once": True,
        "saves": 4,
        "loads": 2,
        "advances": 5,
        "rollback_advances": 1,
        "pair_accepts": 3,
        "local_input_hash": 0xC0,
        "remote_input_hash": 0xD0,
        "local_input_count": 3,
        "remote_input_count": 3,
        "confirmed_canonical_hash": 0xE0,
        "last_restore_expected_hash": 0xF0,
        "last_restore_observed_hash": 0xF0,
        "corrected_frame": 12,
        "confirmed_frame": 12,
    }
    status.update(overrides)
    return status


class NavigationReportSelfTest(unittest.TestCase):
    def test_semantic_dispatch_support_does_not_require_xinput(self) -> None:
        stage = main_menu_stage(main_menu_xinput_native_poller_hooked=False)
        event = semantic_event(xinput_native_poller_hook_installed=False)

        milestone = report.navigation_milestones(
            "host", stage, event, "player-match-nav"
        )["ui-input-probe"]

        self.assertTrue(milestone["supported"])
        self.assertTrue(milestone["semantic_blueprint_supported"])
        self.assertFalse(milestone["xinput_supported"])
        self.assertEqual(milestone["dispatcher"], "semantic-blueprint")

    def test_semantic_action_start_is_not_action_acknowledgement(self) -> None:
        # action_acknowledged=True mirrors the short-lived legacy trace shape.
        # semantic_action_started must still win and remain distinct evidence.
        event = semantic_event(
            semantic_action_started=True,
            action_acknowledged=True,
        )

        evidence = report.ui_navigation_evidence(event)
        milestone = report.navigation_milestones(
            "host", main_menu_stage(), event, "player-match-nav"
        )["ui-input-probe"]

        self.assertTrue(evidence["semantic_action_started"])
        self.assertFalse(evidence["action_acknowledged"])
        self.assertFalse(milestone["action_acknowledged"])
        self.assertEqual(milestone["status"], "semantic-action-started")

    def test_queued_scene_is_distinct_evidence(self) -> None:
        event = semantic_event(
            dispatch_accepted=False,
            call_success=False,
            target_scene_queued=True,
            action_acknowledged=True,
        )

        evidence = report.ui_navigation_evidence(event)
        milestone = report.navigation_milestones(
            "host", main_menu_stage(), event, "player-match-nav"
        )["ui-input-probe"]

        self.assertTrue(evidence["target_scene_queued"])
        self.assertFalse(evidence["action_acknowledged"])
        self.assertTrue(milestone["target_scene_queued"])
        self.assertFalse(milestone["action_acknowledged"])
        self.assertEqual(milestone["status"], "target-scene-queued")

    def test_newer_ui_failure_overlays_stale_stage_state(self) -> None:
        stage = main_menu_stage(ts_qpc=100)
        event = semantic_event(
            ts_qpc=101,
            navigation_state_name="failed",
            navigation_failure="deadline-exceeded",
            reason="main-menu navigation failed: deadline-exceeded",
            ok=False,
        )

        failure = report.terminal_navigation_failure(stage, event)
        diagnostics = report.navigation_diagnostics(stage, event)

        self.assertIsNotNone(failure)
        assert failure is not None
        self.assertEqual(failure["code"], "ui-input-deadline-exceeded")
        self.assertEqual(
            failure["navigation_state_source"],
            "rollback_main_menu_input_navigation",
        )
        self.assertEqual(diagnostics["navigation_state"], "failed")
        self.assertEqual(
            diagnostics["navigation_failure"], "deadline-exceeded"
        )

    def test_older_or_mismatched_ui_event_does_not_override_stage(self) -> None:
        stage = main_menu_stage(ts_qpc=200)
        older = semantic_event(
            ts_qpc=199,
            navigation_state_name="failed",
            navigation_failure="deadline-exceeded",
        )
        mismatched = semantic_event(
            ts_qpc=201,
            request_id="different-request",
            navigation_state_name="failed",
            navigation_failure="deadline-exceeded",
        )

        self.assertIsNone(report.terminal_navigation_failure(stage, older))
        self.assertIsNone(report.terminal_navigation_failure(stage, mismatched))

    def test_active_player_match_scene_suppresses_stale_ui_failure(self) -> None:
        stage = main_menu_stage(
            current_scene_class="PlayerMatchLobbyScene_C",
            current_scene_name="PlayerMatchLobbyScene_C_0",
        )
        event = semantic_event(
            navigation_state_name="failed",
            navigation_failure="deadline-exceeded",
        )

        self.assertIsNone(report.terminal_navigation_failure(stage, event))

    def test_mirrored_proof_accepts_complete_production_evidence(self) -> None:
        expected = {
            "client_role": "host",
            "local_player_slot": 0,
            "native_input_source_slot": 0,
            "network_profile": "clean_0ms",
            "fault_seed": 0x5C6B0001,
        }
        launch = {"state": "complete", "failure": "ok"}

        self.assertEqual(
            report.mirrored_versus_failures(
                production_status(), launch, expected, "rollback-proof"
            ),
            [],
        )

    def test_mirrored_proof_rejects_restore_and_slot_mismatch(self) -> None:
        expected = {
            "client_role": "sandbox",
            "local_player_slot": 1,
            "native_input_source_slot": 0,
            "network_profile": "clean_0ms",
            "fault_seed": 0x5C6B0001,
        }
        failures = report.mirrored_versus_failures(
            production_status(
                gekko_slot=0,
                prediction_restore_verified=False,
                last_restore_observed_hash=0xF1,
            ),
            {"state": "complete", "failure": "ok"},
            expected,
            "rollback-proof",
        )

        self.assertIn("production_gekko_slot", failures)
        self.assertIn("production_prediction_restore_verified", failures)
        self.assertIn("production_final_restore_hash_mismatch", failures)

    def test_mirrored_proof_requires_requested_profile_effect(self) -> None:
        expected = {
            "client_role": "host",
            "local_player_slot": 0,
            "native_input_source_slot": 0,
            "network_profile": "corrupt_probe",
            "fault_seed": 0x5C6B0001,
        }
        status = production_status(
            network_profile="corrupt_probe",
            fault_queued=8,
        )
        failures = report.mirrored_versus_failures(
            status, {"state": "complete", "failure": "ok"},
            expected, "rollback-proof",
        )
        self.assertIn(
            "production_profile_effect:corrupt_probe:fault_corrupted",
            failures,
        )
        status["fault_corrupted"] = 1
        self.assertEqual(
            report.mirrored_versus_failures(
                status, {"state": "complete", "failure": "ok"},
                expected, "rollback-proof",
            ),
            [],
        )

    def test_mirrored_pair_requires_crossed_input_streams(self) -> None:
        host_status = production_status(gekko_slot=0)
        sandbox_status = production_status(
            gekko_slot=1,
            local_input_hash=host_status["remote_input_hash"],
            remote_input_hash=host_status["local_input_hash"],
        )
        results = [
            {
                "root": {"role": "host"},
                "production_status": host_status,
            },
            {
                "root": {"role": "sandbox"},
                "production_status": sandbox_status,
            },
        ]

        self.assertEqual(
            report.mirrored_versus_pair_failures(results, "rollback-proof"),
            [],
        )
        sandbox_status["remote_input_hash"] = 1
        self.assertIn(
            "pair_mirrored_host_local_not_sandbox_remote",
            report.mirrored_versus_pair_failures(results, "rollback-proof"),
        )

    def test_mirrored_request_serializes_production_contract(self) -> None:
        text = report.request_text(
            enabled=True,
            trace=True,
            case="production",
            request_id="mirrored-contract",
            rollback_window=12,
            seed="0x5C6B0001",
            mode="mirrored-versus",
            production_enabled=True,
            bind_port=47160,
            peer_port=47161,
            local_player_slot=0,
            native_input_source_slot=0,
            lifecycle_mode="mirrored-versus",
            production_local_peer=0xA0,
            production_remote_peer=0xB0,
            secret="authenticated-test-secret",
            expected_build_id=0x1135D62F163558E1,
            expected_schema_id=0x06A848479E5A8E91,
        )

        for expected_line in (
            "production_enabled=1",
            "local_player_slot=0",
            "native_input_source_slot=0",
            "lifecycle_mode=mirrored-versus",
            "expected_build_id=0x1135D62F163558E1",
            "expected_schema_id=0x6A848479E5A8E91",
        ):
            self.assertIn(expected_line, text)

    def test_lobby_gate_requires_guest_membership(self) -> None:
        stage = main_menu_stage(
            client_role="sandbox",
            current_scene_class="PlayerMatchLobbyScene_C",
            current_scene_name="PlayerMatchLobbyScene_C_0",
            online_stage_requested=True,
            game_thread=True,
            background_idle_override_value_read=True,
            background_idle_override_command_ok=True,
            online_stage_goal="player-match-lobby",
            membership_ready=False,
        )
        expected = {
            "client_role": "sandbox",
            "online_stage_goal": "player-match-lobby",
        }
        failures = report.online_stage_gate_failures(
            stage, expected, "player-match-lobby"
        )
        self.assertIn("online_stage_membership_ready", failures)
        self.assertIn("online_stage_exact_host_result_not_selected", failures)

    def test_invite_fallback_rejects_raw_steam_membership(self) -> None:
        stage = main_menu_stage(
            client_role="sandbox",
            current_scene_class="PlayerMatchLobbyScene_C",
            current_scene_name="PlayerMatchLobbyScene_C_0",
            online_stage_requested=True,
            game_thread=True,
            background_idle_override_value_read=True,
            background_idle_override_command_ok=True,
            online_stage_goal="player-match-lobby",
            stock_join_route="invite-fallback",
            membership_ready=True,
            steam_join_lobby_ok=True,
            stock_offer_valid=True,
            stock_offer_lobby_id=0x111,
            native_named_session_lobby_id=0,
            stock_metadata_request_ok=False,
            stock_native_event_dispatched=False,
            stock_native_bridge_complete=False,
        )
        expected = {
            "client_role": "sandbox",
            "online_stage_goal": "player-match-lobby",
            "stock_join_route": "invite-fallback",
        }
        failures = report.online_stage_gate_failures(
            stage, expected, "player-match-lobby"
        )
        self.assertIn("online_stage_stock_native_bridge_complete", failures)
        self.assertIn("online_stage_exact_host_result_not_selected", failures)

    def test_invite_fallback_accepts_full_native_bridge(self) -> None:
        stage = main_menu_stage(
            client_role="sandbox",
            current_scene_class="PlayerMatchLobbyScene_C",
            current_scene_name="PlayerMatchLobbyScene_C_0",
            online_stage_requested=True,
            game_thread=True,
            background_idle_override_value_read=True,
            background_idle_override_command_ok=True,
            online_stage_goal="player-match-lobby",
            stock_join_route="invite-fallback",
            membership_ready=True,
            steam_join_lobby_ok=True,
            stock_offer_valid=True,
            stock_offer_lobby_id=0x111,
            native_named_session_lobby_id=0x111,
            stock_metadata_request_ok=True,
            stock_native_event_dispatched=True,
            stock_native_bridge_complete=True,
        )
        expected = {
            "client_role": "sandbox",
            "online_stage_goal": "player-match-lobby",
            "stock_join_route": "invite-fallback",
        }
        self.assertEqual(
            report.online_stage_gate_failures(
                stage, expected, "player-match-lobby"
            ),
            [],
        )

    def test_lobby_gate_accepts_created_host_membership(self) -> None:
        stage = main_menu_stage(
            client_role="host",
            current_scene_class="PlayerMatchLobbyScene_C",
            current_scene_name="PlayerMatchLobbyScene_C_0",
            online_stage_requested=True,
            game_thread=True,
            background_idle_override_value_read=True,
            background_idle_override_command_ok=True,
            online_stage_goal="player-match-lobby",
            create_callback_result=True,
            host_room_create_make_room_ok=True,
            host_room_create_private_room_down_count=9,
            host_room_create_private_room_right_count=1,
            host_room_create_private_room_up_count=9,
            host_room_create_private_room_enabled=True,
            host_room_create_private_room_readback_ok=True,
            host_room_create_private_room_readback_value=1,
            host_room_create_decide_ok=True,
            host_room_create_make_connecting_poll_ok=True,
        )
        expected = {
            "client_role": "host",
            "online_stage_goal": "player-match-lobby",
        }
        self.assertEqual(
            report.online_stage_gate_failures(
                stage, expected, "player-match-lobby"
            ),
            [],
        )

    def test_lobby_gate_rejects_native_only_host_creation(self) -> None:
        stage = main_menu_stage(
            client_role="host",
            current_scene_class="PlayerMatchLobbyScene_C",
            current_scene_name="PlayerMatchLobbyScene_C_0",
            online_stage_requested=True,
            game_thread=True,
            background_idle_override_value_read=True,
            background_idle_override_command_ok=True,
            online_stage_goal="player-match-lobby",
            create_callback_result=True,
        )
        expected = {
            "client_role": "host",
            "online_stage_goal": "player-match-lobby",
        }
        failures = report.online_stage_gate_failures(
            stage, expected, "player-match-lobby"
        )
        self.assertIn("online_stage_host_room_create_make_room_ok", failures)
        self.assertIn(
            "online_stage_host_room_create_private_room_enabled", failures
        )
        self.assertIn(
            "online_stage_host_room_create_private_room_down_count", failures
        )
        self.assertIn(
            "online_stage_host_room_create_private_room_right_count", failures
        )
        self.assertIn(
            "online_stage_host_room_create_private_room_up_count", failures
        )
        self.assertIn("online_stage_host_room_create_decide_ok", failures)
        self.assertIn(
            "online_stage_host_room_create_make_connecting_poll_ok", failures
        )

    def test_lobby_pair_requires_same_native_lobby(self) -> None:
        results = [
            {
                "root": {"role": "host"},
                "online_stage": {"native_named_session_lobby_id": "0x123"},
            },
            {
                "root": {"role": "sandbox"},
                "online_stage": {"native_named_session_lobby_id": "0x456"},
            },
        ]
        failures = report.online_stage_membership_pair_failures(results)
        self.assertIn(
            "pair_online_stage_lobby_mismatch=0x123/0x456", failures
        )
        results[1]["online_stage"]["native_named_session_lobby_id"] = "0x123"
        self.assertEqual(
            report.online_stage_membership_pair_failures(results), []
        )


class ReportContractSelfTest(unittest.TestCase):
    def test_successful_partial_workflow_is_incomplete(self) -> None:
        fields = contract_fields(
            workflow_kind="two-client-acceptance",
            workflow_ok=True,
            coverage_result=coverage(["inventory", "proof"], ["inventory"]),
            acceptance_workflow=True,
        )
        self.assertEqual(fields["verdict"], "incomplete")
        self.assertFalse(fields["coverage_complete"])
        self.assertFalse(fields["acceptance_executed"])
        self.assertIsNone(fields["acceptance_ok"])

    def test_setup_only_is_never_acceptance(self) -> None:
        fields = contract_fields(
            workflow_kind="release-gate",
            workflow_ok=True,
            coverage_result=coverage([], []),
            setup_only=True,
            acceptance_workflow=True,
        )
        self.assertEqual(fields["verdict"], "setup-ready")
        self.assertFalse(fields["acceptance_executed"])

    def test_complete_acceptance_passes(self) -> None:
        fields = contract_fields(
            workflow_kind="two-client-acceptance",
            workflow_ok=True,
            coverage_result=coverage(["inventory", "proof"],
                                     ["inventory", "proof"]),
            acceptance_workflow=True,
        )
        self.assertEqual(validate_v2(fields), [])
        self.assertEqual(fields["verdict"], "pass")
        self.assertTrue(fields["acceptance_executed"])
        self.assertTrue(fields["acceptance_ok"])

    def test_external_contract_contradictions_are_rejected(self) -> None:
        fields = contract_fields(
            workflow_kind="two-client-acceptance",
            workflow_ok=True,
            coverage_result=coverage(["inventory"], ["inventory"]),
            acceptance_workflow=True,
        )
        fields["acceptance_ok"] = None
        fields["coverage"]["missing"] = ["invented"]
        failures = validate_v2(fields)
        self.assertIn("acceptance_ok is inconsistent", failures)
        self.assertIn("coverage missing list is inconsistent", failures)


if __name__ == "__main__":
    unittest.main()
