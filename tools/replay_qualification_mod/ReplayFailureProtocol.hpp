#pragma once
#include <map>
#include <string>
#include <string_view>

// Dedicated ordinary-forward observation protocol. Whitelisting prevents this
// diagnostic from acquiring executor, historical, mutation or seek semantics.
inline bool ReadReplayC18DiagnosticProtocol(const std::map<std::string,std::string>& fields,bool& output)
{
    output=false;
    const auto get=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    if(get("version")!="19")return !fields.contains("c18_diagnostic") && !fields.contains("c18_selection");
    if(get("c18_diagnostic")!="true" || get("capture_mode")!="trajectory" || get("watch_frames")!="360"
        || get("skip_intros")!="true" || get("include_setup")!="true"
        || get("c18_selection")!="combat_170_220")return false;
    for(const auto& [key,value]:fields) {
        if(key!="version" && key!="run_id" && key!="replay_path" && key!="watch_frames"
            && key!="capture_mode" && key!="c18_diagnostic" && key!="skip_intros"
            && key!="include_setup" && key!="flush_startup_loading" && key!="c18_selection")return false;
        if(key=="flush_startup_loading" && value!="true")return false;
    }
    output=true;return true;
}

// Shared bounded coordinate parsing. No mutation of optional request fields.
inline bool ReadCheckpointWindowCoordinate(const std::map<std::string,std::string>& fields,
    const char* key,unsigned& output)
{
    const auto found=fields.find(key);
    if(found==fields.end() || found->second.empty() || found->second.size()>5
        || (found->second.size()>1 && found->second.front()=='0'))return false;
    unsigned value{};
    for(const auto c:found->second) {if(c<'0'||c>'9')return false;value=value*10+unsigned(c-'0');}
    if(value>35000)return false;
    output=value;return true;
}
// One short unchanged-input checkpoint transaction. This is observer scope,
// never a waiver of runtime capture, ownership, publication or commit guards.
inline bool IsBoundedCheckpointWindow(const std::map<std::string,std::string>& fields)
{
    unsigned a{},b{},target{};
    if(!ReadCheckpointWindowCoordinate(fields,"historical_anchor_tick",a)
        || !ReadCheckpointWindowCoordinate(fields,"historical_advanced_tick",b)
        || !ReadCheckpointWindowCoordinate(fields,"host_seek_target",target)
        || (a<221 && !(a==210 && b==217)) || b<=a || b-a>120 || target<a || target>b)return false;
    for(const auto* key:{"host_seek","historical_exact_advance"}) {
        const auto found=fields.find(key);if(found==fields.end()||found->second!="true")return false;
    }
    if(const auto cancel=fields.find("historical_cancel");cancel!=fields.end()
        && cancel->second!="before" && cancel->second!="after")return false;
    for(const auto* key:{"seek_advance_failure","seek_settlement_failure",
        "seek_drained_cancel","corrected_inputs","changed_inputs","source_revision","host_seek_repeat",
        "host_seek_first_target","checkpoint_pair","checkpoint_fallback","capture_cancel","restore_reuse",
        "historical_single_step","completion_repeat","seek_observer_failure","seek_preparation_failure",
        "pixel_diagnostics","pass_diagnostics","same_process_render"})
        if(fields.contains(key))return false;
    return true;
}

// Read-only HUD/pose window shared by candidate and independent control.
// It selects observations only; no execution or seek coordinate is changed.
inline bool ReadReplayObservationTarget(const std::map<std::string,std::string>& fields,unsigned& output)
{
    const auto found=fields.find("observation_target");
    if(found==fields.end()) {output=0;return true;}
    const auto skip=fields.find("skip_intros");
    const auto& text=found->second;
    if(skip==fields.end() || skip->second!="true" || text.empty() || text.size()>5)return false;
    unsigned target{};
    for(const auto c:text) {if(c<'0' || c>'9')return false;target=target*10+unsigned(c-'0');}
    if(target<170 || target>35880)return false;
    output=target;return true;
}

inline bool IsBoundedPrivateHudRecovery(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    return value("historical_anchor_tick")=="170" && value("historical_advanced_tick")=="300"
        && value("host_seek_target")=="208" && value("host_seek")=="true"
        && (value("historical_cancel")=="before" || value("historical_cancel")=="after")
        && !fields.contains("corrected_inputs") && !fields.contains("changed_inputs")
        && !fields.contains("source_revision") && !fields.contains("host_seek_repeat")
        && !fields.contains("seek_advance_failure") && !fields.contains("seek_settlement_failure");
}

inline bool IsBoundedPrivateHudDrainedRecovery(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    return value("historical_anchor_tick")=="170" && value("historical_advanced_tick")=="300"
        && value("host_seek_target")=="300" && value("host_seek")=="true"
        && value("historical_cancel")=="after" && value("seek_advance_failure")=="true"
        && value("seek_settlement_failure")=="true" && value("seek_drained_cancel")=="true"
        && !fields.contains("corrected_inputs") && !fields.contains("changed_inputs")
        && !fields.contains("source_revision") && !fields.contains("host_seek_repeat")
        && !fields.contains("seek_observer_failure") && !fields.contains("seek_preparation_failure");
}

// Exact visible-B trace recovery from the retained repeated-seek failure.
inline bool IsBoundedTraceRenderRecovery(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    return value("historical_anchor_tick")=="170" && (value("historical_advanced_tick")=="329" || value("historical_advanced_tick")=="5695" || value("historical_advanced_tick")=="6538")
        && value("host_seek_target")=="2510" && value("host_seek")=="true"
        && value("historical_cancel")=="after" && value("seek_advance_failure")=="true"
        && value("seek_settlement_failure")=="true" && value("seek_drained_cancel")=="true"
        && !fields.contains("corrected_inputs") && !fields.contains("changed_inputs")
        && !fields.contains("source_revision") && !fields.contains("host_seek_repeat")
        && !fields.contains("seek_observer_failure") && !fields.contains("seek_preparation_failure");
}

// Bounded irreversible debris disposal after completed target tails. This only
// selects the experiment; production lifetime and commit admission stay intact.
inline bool IsBoundedGroundCommit(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    if(value("historical_anchor_tick")!="170" || value("historical_advanced_tick")!="6538"
        || value("host_seek_target")!="2510" || value("host_seek")!="true")return false;
    for(const auto* key:{"historical_cancel","seek_advance_failure","seek_settlement_failure",
        "seek_drained_cancel","corrected_inputs","changed_inputs","source_revision",
        "host_seek_repeat","seek_observer_failure","seek_preparation_failure"})
        if(fields.contains(key))return false;
    return true;
}

// Correct a behaviorally exercised suffix and resimulate to original B300.
// This is a bounded observer protocol, not a new runtime admission exception.
inline bool IsBoundedSameOriginGuard(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    return value("historical_anchor_tick")=="170" && value("historical_advanced_tick")=="300"
        && value("host_seek_target")=="300" && value("host_seek")=="true"
        && value("corrected_inputs")=="true" && value("source_revision")=="true"
        && value("source_revision_profile")=="guard201"
        && !fields.contains("historical_cancel") && !fields.contains("host_seek_repeat")
        && !fields.contains("seek_advance_failure") && !fields.contains("seek_settlement_failure");
}

// Read optional request fields without inserting them: insertion changes later
// contains() checks and can turn an omitted default into an explicit invalid field.
inline bool IsBoundedEmitterRecovery(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key,std::string_view fallback={}) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?fallback:std::string_view(it->second);
    };
    return value("historical_anchor_tick","205")=="205" && value("historical_advanced_tick","210")=="210"
        && value("host_seek_target","208")=="208" && value("seek_settlement_failure")=="true";
}

// A205/B210 ->220 watches a real completion veto; it never injects a failure.
inline bool IsBoundedParticleLifetimeRecovery(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key,std::string_view fallback={}) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?fallback:std::string_view(it->second);
    };
    return value("historical_anchor_tick","205")=="205" && value("historical_advanced_tick","210")=="210"
        && value("host_seek_target")=="220" && value("seek_advance_failure")=="true"
        && value("historical_cancel")=="after" && !fields.contains("seek_settlement_failure");
}

// Two live checkpoints; native particle completion must force a recovered retry.
inline bool IsBoundedExecutionFallback(const std::map<std::string,std::string>& fields)
{
    const auto value=[&](const char* key) -> std::string_view {
        const auto it=fields.find(key);return it==fields.end()?std::string_view{}:std::string_view(it->second);
    };
    return value("historical_anchor_tick")=="170" && value("historical_advanced_tick")=="210"
        && value("host_seek_target")=="220" && value("checkpoint_fallback")=="true"
        && !fields.contains("historical_cancel") && !fields.contains("host_seek_repeat")
        && !fields.contains("seek_advance_failure") && !fields.contains("seek_settlement_failure")
        && !fields.contains("corrected_inputs") && !fields.contains("changed_inputs");
}
