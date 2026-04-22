// ============================================================================
// HorseMod — SoulCalibur VI hit-volume visualiser (UE4SS C++ mod).
//
// What this shows
// ---------------
// Every frame, read the SC6 legacy Namco KHit linked lists on both charas
// and draw their contents as wire-frame volumes in world space using
// UWorld's ULineBatchComponent.
//
//   HURTBOXES  (KHit list at chara+0x44498, count at +0x444B0)
//              On by default.  Green; flashes red when that slot's
//              PerHurtboxReactionState[i] is non-zero (i.e. just got hit).
//
//   ATTACK     (KHit list at chara+0x44478)
//   BOXES      On by default.  Dim amber; the node pointed to by
//              CurrentActiveAttackCell (chara+0x44048) is drawn bright
//              yellow (that's the "hot" attack for the current frame).
//
//   BODY /     (KHit list at chara+0x444B8, count at +0x444B4)
//   PUSHBOX    OFF by default (visually noisy — spacing context only).
//              Enable per-player from the ImGui tab.
//
// Visibility is per-player: P1 and P2 each get independent hurtbox /
// attack / body toggles, so e.g. you can hide P2's pushboxes while
// keeping P1's visible.
//
// All geometry comes from Pipeline 2 (legacy KHit chain) — the same data
// the hit resolver uses.  Weapon-trail Pipeline 1 (ActiveAttackSlot via
// TraceManager) is not drawn in this build; most hit volumes of interest
// (kicks, hurtboxes, bodies) are in Pipeline 2 anyway.
//
// Hotkeys
//   F5  toggle overlay on / off
//
// ImGui tab ("HorseMod")
//   • overlay enable toggle (mirrors F5)
//   • per-list visibility toggles (hurtbox / attack / body)
//   • line thickness slider
//   • LineBatcher slot selector (Default / Persistent / Foreground)
//
// Everything else the earlier prototype had — predicate hooks, bounds
// traces, yarare watchers, process-event spies, cockpit-widget backend,
// capsule walker — has been removed.  If you want any of that back, pull
// it out of git history; it's all on dead paths for the "draw the real
// hitboxes" goal we're pursuing now.
//
// Ghidra references
//   LuxBattle_TickHitResolutionAndBodyCollision  @ 0x14033CCA0  (full plate)
//   ALuxBattleChara_GetBoneTransformForPose      @ 0x140462760
//   LuxSkeletalBoneIndex_Remap                   @ 0x140898140
//   KHitBase / KHitArea / KHitSphere / KHitFixArea — 0xA0 bytes each
// ============================================================================

#include "horselib/HorseLib.hpp"
#include "horselib/KHitWalker.hpp"
#include "horselib/LineBatcherBackend.hpp"
#include "horselib/NativeBinding.hpp"

#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Input/KeyDef.hpp>
#include <Input/Handler.hpp>

#include <imgui.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

using namespace RC;
using namespace RC::Unreal;

// ----------------------------------------------------------------------------
class HorseMod final : public CppUserModBase
{
private:
    // Static live-instance pointer so the cockpit hook lambda can safely
    // no-op after destruction (game thread could fire after Restart All).
    static inline std::atomic<HorseMod*> s_instance{nullptr};

    // ---- Overlay state ----
    std::atomic<bool> m_enabled{false};

    // Per-player visibility toggles.  Hurtbox + Attack default ON, Body
    // defaults OFF (it's visually noisy — spacing context only).  Each
    // player indexed by PlayerIndex (0 = P1, 1 = P2).
    std::atomic<bool> m_show_p1_hurt{true};
    std::atomic<bool> m_show_p1_atk {true};
    std::atomic<bool> m_show_p1_body{false};
    std::atomic<bool> m_show_p2_hurt{true};
    std::atomic<bool> m_show_p2_atk {true};
    std::atomic<bool> m_show_p2_body{false};

    std::atomic<float> m_thickness{2.0f};
    std::atomic<Horse::LineBatcherSlot> m_slot{Horse::LineBatcherSlot::Foreground};

    // Look up the current show-flag for (player, list).  Inlined in the
    // hot path; pi outside [0,1] falls through to P1's settings.
    bool shouldShow(int pi, Horse::KHitList list) const
    {
        const bool is_p2 = (pi == 1);
        switch (list)
        {
            case Horse::KHitList::Hurtbox:
                return (is_p2 ? m_show_p2_hurt : m_show_p1_hurt).load();
            case Horse::KHitList::Attack:
                return (is_p2 ? m_show_p2_atk  : m_show_p1_atk ).load();
            case Horse::KHitList::Body:
                return (is_p2 ? m_show_p2_body : m_show_p1_body).load();
        }
        return false;
    }

    // ---- Hook / backend ----
    bool                         m_hook_registered = false;
    std::pair<int32_t, int32_t>  m_hook_ids{};
    StringType                   m_hook_path;
    int                          m_poll_counter = 0;
    int                          m_update_calls = 0;
    int                          m_diag_tick    = 0;

    Horse::Lux                 m_lux;
    Horse::LineBatcherBackend  m_backend;

    // One-shot log flags so UE4SS.log doesn't fill with repeats.
    bool m_logged_native_missing = false;

public:
    HorseMod() : CppUserModBase()
    {
        ModName        = STR("HorseMod");
        ModVersion     = STR("0.10.0");
        ModDescription = STR("SC6 KHit hitbox / hurtbox / body visualiser.");
        ModAuthors     = STR("horse");

        Input::ModifierKeyArray no_mods{};
        no_mods.fill(Input::ModifierKey::MOD_KEY_START_OF_ENUM);

        register_keydown_event(Input::Key::F5, no_mods, [this]() {
            bool s = !m_enabled.load();
            m_enabled.store(s);
            Output::send<LogLevel::Verbose>(STR("[HorseMod] overlay {}\n"),
                s ? STR("ON") : STR("OFF"));
            if (!s) m_backend.hideAll();
        });

        register_tab(STR("HorseMod"), [](CppUserModBase* inst) {
            static_cast<HorseMod*>(inst)->render_tab_impl();
        });

        s_instance.store(this);
        Output::send<LogLevel::Verbose>(
            STR("[HorseMod] ctor v0.10.0 (KHit walker)\n"));
    }

    ~HorseMod() override
    {
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor ENTER\n"));

        // Zero instance pointer FIRST so any in-flight hook sees null.
        s_instance.store(nullptr);

        if (m_hook_registered && !m_hook_path.empty())
        {
            UObjectGlobals::UnregisterHook(m_hook_path, m_hook_ids);
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] dtor unregistered cockpit hook pre={} post={}\n"),
                m_hook_ids.first, m_hook_ids.second);
        }
        Output::send<LogLevel::Verbose>(STR("[HorseMod] dtor EXIT\n"));
    }

    auto on_ui_init() -> void override { UE4SS_ENABLE_IMGUI() }

    auto on_unreal_init() -> void override
    {
        // Resolve SC6 native function RVAs now that the game image is loaded:
        //   ALuxBattleChara_GetBoneTransformForPose @ image + 0x462760
        //   LuxSkeletalBoneIndex_Remap              @ image + 0x898140
        // These are used by KHitWalker to transform KHitArea OBBs into
        // world space via the owning chara's bone pose.
        Horse::NativeBinding::resolve();
    }

    auto on_update() -> void override
    {
        if (m_hook_registered) return;
        if (++m_poll_counter < 60) return;
        m_poll_counter = 0;
        try_register_cockpit_hook();
    }

private:
    void try_register_cockpit_hook()
    {
        Horse::Obj cockpit = m_lux.cockpit();
        if (!cockpit) return;

        UClass* klass = cockpit.raw()->GetClassPrivate();
        if (!klass) return;

        m_hook_path = klass->GetPathName() + STR(":Update");
        Output::send<LogLevel::Verbose>(STR("[HorseMod] Registering hook: {}\n"), m_hook_path);

        UnrealScriptFunctionCallable pre_cb =
            [](UnrealScriptFunctionCallableContext& ctx, void*) {
                if (auto* self = s_instance.load(std::memory_order_acquire))
                    self->on_cockpit_update_pre(ctx.Context);
            };
        UnrealScriptFunctionCallable post_cb =
            [](UnrealScriptFunctionCallableContext&, void*) {};

        m_hook_ids        = UObjectGlobals::RegisterHook(m_hook_path, pre_cb, post_cb, nullptr);
        m_hook_registered = true;
        Output::send<LogLevel::Verbose>(STR("[HorseMod] hook pre={} post={}\n"),
            m_hook_ids.first, m_hook_ids.second);
    }

    // ------------------------------------------------------------------
    // CockpitBase_C::Update pre-hook.  Game thread, one call per frame.
    // ------------------------------------------------------------------
    void on_cockpit_update_pre(UObject* raw_cockpit)
    {
        ++m_update_calls;

        // Always-on heartbeat: once every ~2s print a single line so we can
        // confirm the hook is ticking even while the overlay is off.  This
        // separates "hook not firing" from "F5 not toggled".
        if ((m_update_calls & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod.Tick] calls={} enabled={} pivot=0x{:x} "
                    "backend_ready={} native_ready={}\n"),
                m_update_calls,
                m_enabled.load() ? 1 : 0,
                reinterpret_cast<uintptr_t>(raw_cockpit),
                m_backend.isReady() ? 1 : 0,
                Horse::NativeBinding::isReady() ? 1 : 0);
        }

        if (!m_enabled.load() || !raw_cockpit) return;
        if (!Horse::NativeBinding::isReady())
        {
            if (!m_logged_native_missing)
            {
                Output::send<LogLevel::Warning>(
                    STR("[HorseMod] NativeBinding not ready — KHit draw disabled\n"));
                m_logged_native_missing = true;
            }
            return;
        }

        Horse::Obj pivot{raw_cockpit};

        // Sync LineBatcher slot with the ImGui toggle.
        if (m_backend.slot() != m_slot.load())
            m_backend.setSlot(m_slot.load());
        m_backend.primeFrom(pivot);
        if (!m_backend.isReady()) return;

        m_backend.beginFrame();

        const float T = m_thickness.load();

        int charas_seen = 0;
        int nodes_drawn = 0;

        m_lux.forEachChara([&](int i, Horse::Obj chara) {
            if (i >= 2) return;  // only P1 / P2; ignore spectators
            ++charas_seen;
            int32_t pi = chara.getValueOr<int32_t>(L"PlayerIndex", i);
            if (pi < 0 || pi > 1) pi = i;

            // Snapshot this player's three toggles once per chara so the
            // inner-loop gate is branch-free.
            const bool show_hurt = shouldShow(pi, Horse::KHitList::Hurtbox);
            const bool show_atk  = shouldShow(pi, Horse::KHitList::Attack );
            const bool show_body = shouldShow(pi, Horse::KHitList::Body   );
            if (!show_hurt && !show_atk && !show_body) return;

            // KHit +0x60 values are already in absolute UE4 world space
            // (Z-up, cm) — the game's per-frame update bakes chara position,
            // yaw, and bone-local into the matrix that wrote them.  The
            // walker just reads them raw; no xform parameter needed.
            Horse::KHitWalker::forEachKHit(
                chara.raw(),
                static_cast<uint32_t>(pi),
                [&](const Horse::KHitDraw& d) {
                    // Gate by list kind using THIS chara's per-player flags.
                    switch (d.list)
                    {
                        case Horse::KHitList::Hurtbox: if (!show_hurt) return; break;
                        case Horse::KHitList::Attack:  if (!show_atk ) return; break;
                        case Horse::KHitList::Body:    if (!show_body) return; break;
                    }

                    const Horse::FLinColor col = colourFor(d, pi);
                    Horse::DrawKHitDraw(m_backend, d, col, T);
                    ++nodes_drawn;
                });
        });

        m_backend.endFrame();

        // Once per ~2s: dump a summary so we can tell whether each stage
        // fired.  Parallel to the KHitWalker's shouldLog() throttle.
        if ((++m_diag_tick & 0x7F) == 1)
        {
            Output::send<LogLevel::Verbose>(
                STR("[HorseMod] frame pivot=0x{:x} backend_ready={} charas={} drawn={}\n"),
                reinterpret_cast<uintptr_t>(raw_cockpit),
                m_backend.isReady() ? 1 : 0,
                charas_seen,
                nodes_drawn);
        }
    }

    // ------------------------------------------------------------------
    // Colour scheme
    //   Hurtboxes — green (bright red flash when reaction_hot)
    //   Attacks   — orange; bright yellow when is_current_attack
    //   Body/push — dim blue (per-chara tint)
    // A subtle per-player hue nudge keeps P1 / P2 distinguishable when
    // they overlap visually.
    // ------------------------------------------------------------------
    static Horse::FLinColor colourFor(const Horse::KHitDraw& d, int pi)
    {
        const float player_tint = (pi == 1) ? 0.80f : 1.0f;

        switch (d.list)
        {
            case Horse::KHitList::Hurtbox:
                return d.reaction_hot
                    ? Horse::FLinColor{ 1.0f, 0.15f, 0.15f, 1.0f }
                    : Horse::FLinColor{ 0.25f * player_tint,
                                        0.95f,
                                        0.35f * player_tint, 1.0f };

            case Horse::KHitList::Attack:
                return d.is_current_attack
                    ? Horse::FLinColor{ 1.0f, 1.0f, 0.25f, 1.0f }    // hot = yellow
                    : Horse::FLinColor{ 1.0f * player_tint,
                                        0.55f * player_tint,
                                        0.10f, 0.6f };               // cold = amber

            case Horse::KHitList::Body:
            default:
                return Horse::FLinColor{ 0.25f,
                                         0.45f * player_tint,
                                         1.0f * player_tint, 0.5f };
        }
    }

    // ------------------------------------------------------------------
    // ImGui tab
    // ------------------------------------------------------------------
    void render_tab_impl()
    {
        bool enabled = m_enabled.load();
        if (ImGui::Checkbox("Overlay enabled (F5)", &enabled))
        {
            m_enabled.store(enabled);
            if (!enabled) m_backend.hideAll();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("ticks:%d  hook:%s  lbc:%s  native:%s",
            m_update_calls,
            m_hook_registered                ? "OK" : "pending",
            m_backend.isReady()              ? "OK" : "idle",
            Horse::NativeBinding::isReady()  ? "OK" : "miss");

        ImGui::Separator();
        ImGui::TextUnformatted("KHit lists — per player");
        ImGui::TextDisabled(
            "Hurtboxes + Attacks default ON.  Body / pushbox default OFF "
            "(noisy — spacing context only).");

        auto per_player_row = [](const char* label,
                                 std::atomic<bool>& hurt,
                                 std::atomic<bool>& atk,
                                 std::atomic<bool>& body,
                                 const char* id_suffix)
        {
            ImGui::PushID(id_suffix);
            ImGui::TextUnformatted(label);
            ImGui::SameLine(80.0f);
            {
                bool h = hurt.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Hurtboxes##%s", id_suffix);
                if (ImGui::Checkbox(tag, &h)) hurt.store(h);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit HurtboxListHead (chara+0x44498).\n"
                    "Green; red flash on PerHurtboxReactionState non-zero "
                    "(just got hit).");
            }
            ImGui::SameLine();
            {
                bool a = atk.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Attacks##%s", id_suffix);
                if (ImGui::Checkbox(tag, &a)) atk.store(a);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit AttackListHead (chara+0x44478).\n"
                    "Amber = cold; bright yellow = currently-active entry\n"
                    "(CurrentActiveAttackCell @ chara+0x44048).");
            }
            ImGui::SameLine();
            {
                bool b = body.load();
                char tag[32]; std::snprintf(tag, sizeof(tag),
                    "Body##%s", id_suffix);
                if (ImGui::Checkbox(tag, &b)) body.store(b);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                    "KHit BodyListHead (chara+0x444B8).\n"
                    "Dim blue pushbox / collision volumes.");
            }
            ImGui::PopID();
        };

        per_player_row("P1",
                       m_show_p1_hurt, m_show_p1_atk, m_show_p1_body, "p1");
        per_player_row("P2",
                       m_show_p2_hurt, m_show_p2_atk, m_show_p2_body, "p2");

        // Quick-action buttons for the common cases.
        if (ImGui::SmallButton("All ON"))
        {
            m_show_p1_hurt.store(true); m_show_p1_atk.store(true); m_show_p1_body.store(true);
            m_show_p2_hurt.store(true); m_show_p2_atk.store(true); m_show_p2_body.store(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("All OFF"))
        {
            m_show_p1_hurt.store(false); m_show_p1_atk.store(false); m_show_p1_body.store(false);
            m_show_p2_hurt.store(false); m_show_p2_atk.store(false); m_show_p2_body.store(false);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Defaults (no Body)"))
        {
            m_show_p1_hurt.store(true); m_show_p1_atk.store(true); m_show_p1_body.store(false);
            m_show_p2_hurt.store(true); m_show_p2_atk.store(true); m_show_p2_body.store(false);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Render");
        {
            float t = m_thickness.load();
            if (ImGui::SliderFloat("Thickness", &t, 0.5f, 8.0f, "%.1f"))
                m_thickness.store(t);

            const char* slot_names[3] = {
                "Default (depth-tested)",
                "Persistent",
                "Foreground (on top)",
            };
            int slot_idx = static_cast<int>(m_slot.load());
            if (ImGui::Combo("LineBatcher slot", &slot_idx, slot_names, 3))
                m_slot.store(static_cast<Horse::LineBatcherSlot>(slot_idx));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Which of UWorld's three ULineBatchComponents to append to.\n"
                    "  Default     — depth-tested, occluded by world geometry\n"
                    "  Persistent  — depth-tested, identical behaviour per-frame\n"
                    "  Foreground  — NO depth test, always on top (recommended)");
        }
    }
};

// ----------------------------------------------------------------------------
#define HORSE_MOD_API __declspec(dllexport)
extern "C"
{
    HORSE_MOD_API CppUserModBase* start_mod()                    { return new HorseMod(); }
    HORSE_MOD_API void             uninstall_mod(CppUserModBase* m) { delete m; }
}
