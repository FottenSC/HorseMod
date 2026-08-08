#pragma once

#include "RollbackStateHash.hpp"

#include <cstdint>

namespace Horse
{
    static constexpr uint32_t kRollbackReplayOracleSchemaVersion = 13;
    static constexpr uint32_t kRollbackReplayOracleLegacyV2SchemaVersion = 2;
    static constexpr uint32_t kRollbackReplayOracleLegacyV3SchemaVersion = 3;
    static constexpr uint32_t kRollbackReplayOracleLegacyV4SchemaVersion = 4;
    static constexpr uint32_t kRollbackReplayOracleLegacyV5SchemaVersion = 5;
    static constexpr uint32_t kRollbackReplayOracleLegacyV6SchemaVersion = 6;
    static constexpr uint32_t kRollbackReplayOracleLegacyV7SchemaVersion = 7;
    static constexpr uint32_t kRollbackReplayOracleLegacyV8SchemaVersion = 8;
    static constexpr uint32_t kRollbackReplayOracleLegacyV9SchemaVersion = 9;
    static constexpr uint32_t kRollbackReplayOracleLegacyV10SchemaVersion = 10;
    static constexpr uint32_t kRollbackReplayOracleLegacyV11SchemaVersion = 11;
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV2 =
        0x524247504C415932ull; // "RBGPLAY2"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV2 =
        0x5242505245535632ull; // "RBPRESV2"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV3 =
        0x524247504C415933ull; // "RBGPLAY3"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV3 =
        0x5242505245535633ull; // "RBPRESV3"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV4 =
        0x524247504C415934ull; // "RBGPLAY4"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV4 =
        0x5242505245535634ull; // "RBPRESV4"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV5 =
        0x524247504C415935ull; // "RBGPLAY5"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV5 =
        0x5242505245535635ull; // "RBPRESV5"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV6 =
        0x524247504C415936ull; // "RBGPLAY6"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV6 =
        0x5242505245535636ull; // "RBPRESV6"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV7 =
        0x524247504C415937ull; // "RBGPLAY7"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV7 =
        0x5242505245535637ull; // "RBPRESV7"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV8 =
        0x524247504C415938ull; // "RBGPLAY8"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV8 =
        0x5242505245535638ull; // "RBPRESV8"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV9 =
        0x524247504C415939ull; // "RBGPLAY9"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV9 =
        0x5242505245535639ull; // "RBPRESV9"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV10 =
        0x524247504C415941ull; // "RBGPLAYA"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV10 =
        0x5242505245535641ull; // "RBPRESVA"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV11 =
        0x524247504C415942ull; // "RBGPLAYB"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV11 =
        0x5242505245535642ull; // "RBPRESVB"
    static constexpr uint64_t kRollbackReplayOracleGameplayDomainV12 =
        0x524247504C415943ull; // "RBGPLAYC"
    static constexpr uint64_t kRollbackReplayOraclePresentationDomainV12 =
        0x5242505245535643ull; // "RBPRESVC"

    struct RollbackReplayOracleFighterState
    {
        bool readable {false};
        uint32_t current_move_id {0};
        uint32_t current_move_frame {0};
        float pos_x {0.0f};
        float pos_y {0.0f};
        float pos_z {0.0f};
        float vel_x {0.0f};
        float vel_y {0.0f};
        float vel_z {0.0f};
        float facing {0.0f};
        float pose_pos_x {0.0f};
        float pose_pos_y {0.0f};
        float pose_pos_z {0.0f};
        float render_pos_x {0.0f};
        float render_pos_y {0.0f};
        float render_pos_z {0.0f};
        float vital_scale {0.0f};
        float vital_candidate {0.0f};
        float vital_ko_gate {0.0f};
        float vital_displayed {0.0f};
        uint32_t vital_category_bits {0};
        int16_t vital_state {0};
        uint8_t in_hitstun {0};
        uint8_t in_blockstun {0};
        uint32_t hit_reaction_result {0};
        uint8_t soul_charge_mode {0};
        uint8_t soul_charge_state {0};
        uint32_t soul_charge_trigger_bits {0};
        uint32_t soul_charge_kind_group {0};
        uint32_t soul_charge_match_counter {0};
    };

    template<typename Fighter>
    static inline RollbackReplayOracleFighterState
    BuildRollbackReplayOracleFighterState(
        const Fighter& fighter) noexcept
    {
        RollbackReplayOracleFighterState out {};
#define HORSE_COPY_ORACLE_FIGHTER_FIELD(field) out.field = fighter.field
        HORSE_COPY_ORACLE_FIGHTER_FIELD(readable);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(current_move_id);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(current_move_frame);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(pos_x);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(pos_y);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(pos_z);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vel_x);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vel_y);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vel_z);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(facing);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(pose_pos_x);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(pose_pos_y);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(pose_pos_z);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(render_pos_x);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(render_pos_y);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(render_pos_z);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vital_scale);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vital_candidate);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vital_ko_gate);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vital_displayed);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vital_category_bits);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(vital_state);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(in_hitstun);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(in_blockstun);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(hit_reaction_result);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(soul_charge_mode);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(soul_charge_state);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(soul_charge_trigger_bits);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(soul_charge_kind_group);
        HORSE_COPY_ORACLE_FIGHTER_FIELD(soul_charge_match_counter);
#undef HORSE_COPY_ORACLE_FIGHTER_FIELD
        return out;
    }

    struct RollbackReplayOracleState
    {
        // Native replay input is retained at its observed width for
        // diagnostics. Rollback consumes and transports only the low 32 bits,
        // so gameplay comparison must normalize both paths to that authority.
        uint64_t input[2] {};
        uint32_t rng_lcg_state {0};
        uint64_t rng_lfsr_hash {0};
        uint32_t rng_lfsr_index {0};
        bool rng_gameplay_crt_present {false};
        uint32_t rng_gameplay_crt_state {0};
        uint32_t rng_gameplay_crt_seed {0};
        uint32_t rng_gameplay_crt_draw_ordinal {0};
        uint32_t round_control[5] {};
        uint64_t breakable_gameplay_digest {0};
        uint64_t stage_wind_gameplay_digest {0};
        uint64_t chara_animation_gameplay_digest {0};
        uint64_t breakable_presentation_digest {0};
        uint64_t stage_wind_presentation_digest {0};
        RollbackReplayOracleFighterState fighter[2] {};
    };

    static inline void HashRollbackReplayOracleFighter(
        RollbackHash& hash,
        const RollbackReplayOracleFighterState& fighter) noexcept
    {
        hash.add_scalar(fighter.readable);
        hash.add_scalar(fighter.current_move_id);
        hash.add_scalar(fighter.current_move_frame);
        hash.add_scalar(fighter.pos_x);
        hash.add_scalar(fighter.pos_y);
        hash.add_scalar(fighter.pos_z);
        hash.add_scalar(fighter.vel_x);
        hash.add_scalar(fighter.vel_y);
        hash.add_scalar(fighter.vel_z);
        hash.add_scalar(fighter.facing);
        hash.add_scalar(fighter.pose_pos_x);
        hash.add_scalar(fighter.pose_pos_y);
        hash.add_scalar(fighter.pose_pos_z);
        hash.add_scalar(fighter.render_pos_x);
        hash.add_scalar(fighter.render_pos_y);
        hash.add_scalar(fighter.render_pos_z);
        hash.add_scalar(fighter.vital_scale);
        hash.add_scalar(fighter.vital_candidate);
        hash.add_scalar(fighter.vital_ko_gate);
        hash.add_scalar(fighter.vital_displayed);
        hash.add_scalar(fighter.vital_category_bits);
        hash.add_scalar(fighter.vital_state);
        hash.add_scalar(fighter.in_hitstun);
        hash.add_scalar(fighter.in_blockstun);
        hash.add_scalar(fighter.hit_reaction_result);
        hash.add_scalar(fighter.soul_charge_mode);
        hash.add_scalar(fighter.soul_charge_state);
        hash.add_scalar(fighter.soul_charge_trigger_bits);
        hash.add_scalar(fighter.soul_charge_kind_group);
        hash.add_scalar(fighter.soul_charge_match_counter);
    }

    static inline uint64_t HashRollbackReplayOracleStateLegacyV1(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(state.input[0]);
        hash.add_scalar(state.input[1]);
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.breakable_presentation_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV2(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV2);
        hash.add_scalar(kRollbackReplayOracleLegacyV2SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV2(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV2);
        hash.add_scalar(kRollbackReplayOracleLegacyV2SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV3(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV3);
        hash.add_scalar(kRollbackReplayOracleLegacyV3SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV3(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV3);
        hash.add_scalar(kRollbackReplayOracleLegacyV3SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV4(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV4);
        hash.add_scalar(kRollbackReplayOracleLegacyV4SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV4(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV4);
        hash.add_scalar(kRollbackReplayOracleLegacyV4SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV5(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV5);
        hash.add_scalar(kRollbackReplayOracleLegacyV5SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV5(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV5);
        hash.add_scalar(kRollbackReplayOracleLegacyV5SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV6(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV6);
        hash.add_scalar(kRollbackReplayOracleLegacyV6SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV6(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV6);
        hash.add_scalar(kRollbackReplayOracleLegacyV6SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV7(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV7);
        hash.add_scalar(kRollbackReplayOracleLegacyV7SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV7(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV7);
        hash.add_scalar(kRollbackReplayOracleLegacyV7SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV8(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV8);
        hash.add_scalar(kRollbackReplayOracleLegacyV8SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV8(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV8);
        hash.add_scalar(kRollbackReplayOracleLegacyV8SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV9(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV9);
        hash.add_scalar(kRollbackReplayOracleLegacyV9SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        hash.add_scalar(state.chara_animation_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV9(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV9);
        hash.add_scalar(kRollbackReplayOracleLegacyV9SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV10(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV10);
        hash.add_scalar(kRollbackReplayOracleLegacyV10SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        hash.add_scalar(state.chara_animation_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV10(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV10);
        hash.add_scalar(kRollbackReplayOracleLegacyV10SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV11(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV11);
        hash.add_scalar(kRollbackReplayOracleLegacyV11SchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        hash.add_scalar(state.chara_animation_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV11(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV11);
        hash.add_scalar(kRollbackReplayOracleLegacyV11SchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOracleGameplayV12(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOracleGameplayDomainV12);
        hash.add_scalar(kRollbackReplayOracleSchemaVersion);
        hash.add_scalar(static_cast<uint32_t>(state.input[0]));
        hash.add_scalar(static_cast<uint32_t>(state.input[1]));
        hash.add_scalar(state.rng_lcg_state);
        hash.add_scalar(state.rng_lfsr_hash);
        hash.add_scalar(state.rng_lfsr_index);
        hash.add_scalar(state.rng_gameplay_crt_present);
        if (state.rng_gameplay_crt_present)
        {
            hash.add_scalar(state.rng_gameplay_crt_state);
            hash.add_scalar(state.rng_gameplay_crt_seed);
            hash.add_scalar(state.rng_gameplay_crt_draw_ordinal);
        }
        for (uint32_t value : state.round_control)
            hash.add_scalar(value);
        hash.add_scalar(state.breakable_gameplay_digest);
        hash.add_scalar(state.stage_wind_gameplay_digest);
        hash.add_scalar(state.chara_animation_gameplay_digest);
        HashRollbackReplayOracleFighter(hash, state.fighter[0]);
        HashRollbackReplayOracleFighter(hash, state.fighter[1]);
        return hash.value ? hash.value : 1;
    }

    static inline uint64_t HashRollbackReplayOraclePresentationV12(
        const RollbackReplayOracleState& state) noexcept
    {
        RollbackHash hash {};
        hash.add_scalar(kRollbackReplayOraclePresentationDomainV12);
        hash.add_scalar(kRollbackReplayOracleSchemaVersion);
        hash.add_scalar(state.breakable_presentation_digest);
        hash.add_scalar(state.stage_wind_presentation_digest);
        return hash.value ? hash.value : 1;
    }
}
