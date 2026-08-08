#include "../HorseMod/horselib/RollbackReplayOracle.hpp"

#include <cstdio>

int main()
{
    using namespace Horse;

    RollbackReplayOracleState baseline {};
    baseline.input[0] = 0x400u;
    baseline.input[1] = 0x800u;
    baseline.rng_lcg_state = 0x12345678u;
    baseline.rng_lfsr_hash = 0x1122334455667788ull;
    baseline.rng_lfsr_index = 5;
    baseline.rng_gameplay_crt_present = true;
    baseline.rng_gameplay_crt_state = 0xCAFEBABEu;
    baseline.rng_gameplay_crt_seed = 0x12345000u;
    baseline.rng_gameplay_crt_draw_ordinal = 7;
    baseline.round_control[0] = 1;
    baseline.round_control[1] = 3;
    baseline.breakable_gameplay_digest = 0xAABBCCDDu;
    baseline.stage_wind_gameplay_digest = 0x1234ABCDEF987654ull;
    baseline.chara_animation_gameplay_digest = 0x0A11C1A7Eull;
    baseline.breakable_presentation_digest = 0x99887766u;
    baseline.stage_wind_presentation_digest = 0x7766554433221100ull;
    baseline.fighter[0].readable = true;
    baseline.fighter[0].current_move_id = 10;
    baseline.fighter[0].pos_x = 1.25f;
    baseline.fighter[1].readable = true;
    baseline.fighter[1].current_move_id = 20;
    baseline.fighter[1].pos_z = -2.5f;

    const uint64_t gameplay =
        HashRollbackReplayOracleGameplayV12(baseline);
    const uint64_t presentation =
        HashRollbackReplayOraclePresentationV12(baseline);
    const uint64_t legacy_v11_gameplay =
        HashRollbackReplayOracleGameplayV11(baseline);
    const uint64_t legacy_v11_presentation =
        HashRollbackReplayOraclePresentationV11(baseline);
    const uint64_t legacy_v10_gameplay =
        HashRollbackReplayOracleGameplayV10(baseline);
    const uint64_t legacy_v10_presentation =
        HashRollbackReplayOraclePresentationV10(baseline);
    const uint64_t legacy_v8_gameplay =
        HashRollbackReplayOracleGameplayV8(baseline);
    const uint64_t legacy_v8_presentation =
        HashRollbackReplayOraclePresentationV8(baseline);
    const uint64_t legacy_v7_gameplay =
        HashRollbackReplayOracleGameplayV7(baseline);
    const uint64_t legacy_v7_presentation =
        HashRollbackReplayOraclePresentationV7(baseline);
    const uint64_t legacy_v6_gameplay =
        HashRollbackReplayOracleGameplayV6(baseline);
    const uint64_t legacy_v6_presentation =
        HashRollbackReplayOraclePresentationV6(baseline);
    const uint64_t legacy_v5_gameplay =
        HashRollbackReplayOracleGameplayV5(baseline);
    const uint64_t legacy_v5_presentation =
        HashRollbackReplayOraclePresentationV5(baseline);
    const uint64_t legacy =
        HashRollbackReplayOracleStateLegacyV1(baseline);

    RollbackReplayOracleState high_input = baseline;
    high_input.input[0] |= 0x40000000000ull;
    const bool high_input_ok =
        HashRollbackReplayOracleGameplayV12(high_input) == gameplay
        && HashRollbackReplayOraclePresentationV12(high_input) == presentation
        && HashRollbackReplayOracleStateLegacyV1(high_input) != legacy;

    RollbackReplayOracleState low_input = baseline;
    low_input.input[0] ^= 1u;
    const bool low_input_ok =
        HashRollbackReplayOracleGameplayV12(low_input) != gameplay
        && HashRollbackReplayOraclePresentationV12(low_input) == presentation;

    RollbackReplayOracleState presentation_only = baseline;
    presentation_only.breakable_presentation_digest ^= 1u;
    const bool presentation_ok =
        HashRollbackReplayOracleGameplayV12(presentation_only) == gameplay
        && HashRollbackReplayOraclePresentationV12(presentation_only)
            != presentation
        && HashRollbackReplayOracleStateLegacyV1(presentation_only) != legacy;

    RollbackReplayOracleState gameplay_only = baseline;
    gameplay_only.breakable_gameplay_digest ^= 1u;
    const bool gameplay_ok =
        HashRollbackReplayOracleGameplayV12(gameplay_only) != gameplay
        && HashRollbackReplayOraclePresentationV12(gameplay_only)
            == presentation;

    RollbackReplayOracleState stage_wind = baseline;
    stage_wind.stage_wind_gameplay_digest ^= 1u;
    const bool stage_wind_ok =
        HashRollbackReplayOracleGameplayV12(stage_wind) != gameplay
        && HashRollbackReplayOracleGameplayV2(stage_wind)
            == HashRollbackReplayOracleGameplayV2(baseline)
        && HashRollbackReplayOraclePresentationV12(stage_wind)
            == presentation;

    RollbackReplayOracleState stage_wind_presentation = baseline;
    stage_wind_presentation.stage_wind_presentation_digest ^= 1u;
    const bool stage_wind_presentation_ok =
        HashRollbackReplayOracleGameplayV12(stage_wind_presentation)
            == gameplay
        && HashRollbackReplayOraclePresentationV12(
            stage_wind_presentation) != presentation
        && HashRollbackReplayOracleGameplayV5(
            stage_wind_presentation) == legacy_v5_gameplay
        && HashRollbackReplayOraclePresentationV5(
            stage_wind_presentation) == legacy_v5_presentation;

    RollbackReplayOracleState fighter = baseline;
    fighter.fighter[1].pos_z += 0.25f;
    const bool fighter_ok =
        HashRollbackReplayOracleGameplayV12(fighter) != gameplay
        && HashRollbackReplayOraclePresentationV12(fighter) == presentation;

    RollbackReplayOracleState chara_animation = baseline;
    chara_animation.chara_animation_gameplay_digest ^= 1u;
    const bool chara_animation_ok =
        HashRollbackReplayOracleGameplayV12(chara_animation) != gameplay
        && HashRollbackReplayOraclePresentationV12(chara_animation)
            == presentation
        && HashRollbackReplayOracleGameplayV8(chara_animation)
            == legacy_v8_gameplay;

    RollbackReplayOracleState crt_mutation = baseline;
    ++crt_mutation.rng_gameplay_crt_state;
    const bool crt_ok =
        HashRollbackReplayOracleGameplayV12(crt_mutation) != gameplay
        && HashRollbackReplayOracleGameplayV11(crt_mutation)
            == legacy_v11_gameplay
        && HashRollbackReplayOraclePresentationV12(crt_mutation)
            == presentation;

    const bool ok =
        kRollbackReplayOracleSchemaVersion == 13
        && kRollbackReplayOracleLegacyV2SchemaVersion == 2
        && kRollbackReplayOracleLegacyV3SchemaVersion == 3
        && kRollbackReplayOracleLegacyV4SchemaVersion == 4
        && kRollbackReplayOracleLegacyV5SchemaVersion == 5
        && kRollbackReplayOracleLegacyV6SchemaVersion == 6
        && kRollbackReplayOracleLegacyV7SchemaVersion == 7
        && kRollbackReplayOracleLegacyV8SchemaVersion == 8
        && kRollbackReplayOracleLegacyV9SchemaVersion == 9
        && kRollbackReplayOracleLegacyV10SchemaVersion == 10
        && kRollbackReplayOracleLegacyV11SchemaVersion == 11
        && legacy_v11_gameplay != gameplay
        && legacy_v11_presentation != presentation
        && legacy_v10_gameplay != gameplay
        && legacy_v10_presentation != presentation
        && legacy_v8_gameplay != gameplay
        && legacy_v8_presentation != presentation
        && legacy_v7_gameplay != gameplay
        && legacy_v7_presentation != presentation
        && legacy_v6_gameplay != gameplay
        && legacy_v6_presentation != presentation
        && HashRollbackReplayOracleGameplayV2(baseline) != gameplay
        && HashRollbackReplayOraclePresentationV2(baseline) != presentation
        && HashRollbackReplayOracleGameplayV4(baseline) != gameplay
        && HashRollbackReplayOraclePresentationV4(baseline) != presentation
        && gameplay != 0 && presentation != 0 && legacy != 0
        && high_input_ok && low_input_ok && presentation_ok
        && gameplay_ok && stage_wind_ok && stage_wind_presentation_ok
        && fighter_ok && chara_animation_ok && crt_ok;
    if (!ok)
    {
        std::fprintf(stderr,
            "rollback replay oracle self-test failed "
            "high=%d low=%d presentation=%d gameplay=%d "
            "wind=%d wind_presentation=%d fighter=%d animation=%d crt=%d\n",
            high_input_ok ? 1 : 0,
            low_input_ok ? 1 : 0,
            presentation_ok ? 1 : 0,
            gameplay_ok ? 1 : 0,
            stage_wind_ok ? 1 : 0,
            stage_wind_presentation_ok ? 1 : 0,
            fighter_ok ? 1 : 0,
            chara_animation_ok ? 1 : 0,
            crt_ok ? 1 : 0);
        return 1;
    }

    std::puts("rollback replay oracle self-test passed schema=13");
    return 0;
}
