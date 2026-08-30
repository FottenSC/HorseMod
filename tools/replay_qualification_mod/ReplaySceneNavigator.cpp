#include "ReplaySceneNavigator.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <Unreal/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Horse::Qualification
{
namespace
{
using InitPathFn = void* (__fastcall*)(void*);
using DestroyPathFn = void (__fastcall*)(void*);

struct DataTablePath
{
    void* vtable{};
    void* node_ref{};
    void* reference_count{};
};
static_assert(sizeof(DataTablePath) == 0x18);

using UIDataObject = DataTablePath;

struct NullScriptDelegate
{
    std::int32_t object_index{-1};
    std::int32_t object_serial{};
    std::int32_t function_name_index{};
    std::int32_t function_name_number{};
};
static_assert(sizeof(NullScriptDelegate) == 0x10);

struct ChangeSceneParams
{
    RC::Unreal::FString transition_tag;
    DataTablePath inherited_data{};
    NullScriptDelegate change_scene_param{};

    explicit ChangeSceneParams(const wchar_t* tag) : transition_tag(tag) {}
};
static_assert(sizeof(ChangeSceneParams) == 0x38);

InitPathFn g_initialize_path{};
DestroyPathFn g_destroy_path{};

bool SignatureMatches(std::uintptr_t address,
                      const std::array<std::byte, 8>& expected) noexcept
{
    __try { return std::memcmp(reinterpret_cast<void*>(address),
                               expected.data(), expected.size()) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

RC::Unreal::UObject* FindManager() noexcept
{
    RC::Unreal::UObject* manager =
        RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxUIGameFlowManager");
    return manager != nullptr && RC::Unreal::UObject::IsReal(manager)
        ? manager : nullptr;
}

RC::Unreal::FProperty* Param(RC::Unreal::UFunction* function,
                             const wchar_t* name) noexcept
{
    if (function == nullptr || name == nullptr) return nullptr;
    try
    {
        return function->FindProperty(
            RC::Unreal::FName(name, RC::Unreal::FNAME_Find));
    }
    catch (...) { return nullptr; }
}

RC::Unreal::UObject* CurrentScene(RC::Unreal::UObject* manager) noexcept
{
    if (manager == nullptr) return nullptr;
    auto** value = manager->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
        L"CurrentScene");
    return value != nullptr && *value != nullptr
        && RC::Unreal::UObject::IsReal(*value) ? *value : nullptr;
}

RC::Unreal::UObject* ObjectProperty(RC::Unreal::UObject* owner,
                                    const wchar_t* name) noexcept
{
    if (owner == nullptr) return nullptr;
    auto** value = owner->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(
        name);
    return value != nullptr && *value != nullptr
        && RC::Unreal::UObject::IsReal(*value) ? *value : nullptr;
}

bool CallStringParam(RC::Unreal::UObject* owner, const wchar_t* name,
                     const wchar_t* value)
{
    if (owner == nullptr) return false;
    auto* function = owner->GetFunctionByNameInChain(name);
    if (function == nullptr) return false;
    struct Params { RC::Unreal::FString value; };
    Params params{RC::Unreal::FString(value)};
    owner->ProcessEvent(function, &params);
    return true;
}

bool CallIntParam(RC::Unreal::UObject* owner, const wchar_t* name,
                  std::int32_t value)
{
    if (owner == nullptr) return false;
    auto* function = owner->GetFunctionByNameInChain(name);
    if (function == nullptr) return false;
    struct Params { std::int32_t value; };
    Params params{value};
    owner->ProcessEvent(function, &params);
    return true;
}

bool CallNoParam(RC::Unreal::UObject* owner, const wchar_t* name)
{
    if (owner == nullptr) return false;
    auto* function = owner->GetFunctionByNameInChain(name);
    if (function == nullptr) return false;
    owner->ProcessEvent(function, nullptr);
    return true;
}

std::string StringProperty(RC::Unreal::UObject* owner,
                           const wchar_t* name) noexcept
{
    if (owner == nullptr) return {};
    auto* value = owner->GetValuePtrByPropertyNameInChain<RC::Unreal::FString>(
        name);
    return value != nullptr ? RC::to_string(**value) : std::string{};
}

bool ChangeScene(RC::Unreal::UObject* owner, const wchar_t* tag)
{
    auto* function = owner->GetFunctionByNameInChain(L"ChangeScene");
    if (function == nullptr || g_initialize_path == nullptr
        || g_destroy_path == nullptr)
    {
        return false;
    }
    ChangeSceneParams params(tag);
    g_initialize_path(&params.inherited_data);
    owner->ProcessEvent(function, &params);
    g_destroy_path(&params.inherited_data);
    return true;
}

void Destroy(UIDataObject& value) noexcept
{
    if (g_destroy_path != nullptr) g_destroy_path(&value);
    value = {};
}

bool CreateStringDataObject(const wchar_t* value,
                            UIDataObject& output) noexcept
{
    output = {};
    auto* cdo = RC::Unreal::UObjectGlobals::StaticFindObject<
        RC::Unreal::UObject*>(nullptr, nullptr,
        STR("/Script/UMGUtil.Default__UMGUtilUIDataObjectLibrary"));
    if (cdo == nullptr || !RC::Unreal::UObject::IsReal(cdo)) return false;
    auto* function = cdo->GetFunctionByNameInChain(
        L"Conv_StringToUIDataObject");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096)
        return false;
    auto* input = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, L"inString"));
    auto* result = function->GetReturnProperty();
    if (input == nullptr || result == nullptr || result->GetSize() <= 0
        || result->GetSize() > static_cast<std::int32_t>(sizeof(output)))
        return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    bool initialized{};
    try
    {
        input->InitializeValue_InContainer(params.data());
        initialized = true;
        input->SetPropertyValueInContainer(params.data(),
                                            RC::Unreal::FString(value));
        cdo->ProcessEvent(function, params.data());
        void* slot = result->ContainerPtrToValuePtr<void>(params.data());
        if (slot == nullptr)
        {
            input->DestroyValue_InContainer(params.data());
            return false;
        }
        std::memcpy(&output, slot, static_cast<std::size_t>(result->GetSize()));
        input->DestroyValue_InContainer(params.data());
        return true;
    }
    catch (...)
    {
        if (initialized) input->DestroyValue_InContainer(params.data());
        output = {};
        return false;
    }
}

bool RequestReplayList(RC::Unreal::UObject* scene) noexcept
{
    if (scene == nullptr || !RC::Unreal::UObject::IsReal(scene)) return false;
    auto* function = scene->GetFunctionByNameInChain(L"RequestChangeScene");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096)
        return false;
    auto* tag = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, L"inTag"));
    auto* inherited = Param(function, L"inInheritedData");
    if (tag == nullptr || inherited == nullptr || inherited->GetSize() <= 0
        || inherited->GetSize() > static_cast<std::int32_t>(sizeof(UIDataObject)))
        return false;

    UIDataObject data{};
    if (!CreateStringDataObject(L"replaybattle", data)) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    bool initialized{};
    try
    {
        tag->InitializeValue_InContainer(params.data());
        initialized = true;
        tag->SetPropertyValueInContainer(params.data(),
                                         RC::Unreal::FString(L"replaylist"));
        void* slot = inherited->ContainerPtrToValuePtr<void>(params.data());
        if (slot == nullptr)
        {
            tag->DestroyValue_InContainer(params.data());
            Destroy(data);
            return false;
        }
        std::memcpy(slot, &data, static_cast<std::size_t>(inherited->GetSize()));
        scene->ProcessEvent(function, params.data());
        tag->DestroyValue_InContainer(params.data());
        Destroy(data);
        return true;
    }
    catch (...)
    {
        if (initialized) tag->DestroyValue_InContainer(params.data());
        Destroy(data);
        return false;
    }
}
}

bool ReplaySceneNavigator::Bind(std::uintptr_t image_base) noexcept
{
    constexpr std::array<std::byte, 8> kInit{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x10}, std::byte{0x48}, std::byte{0x89}, std::byte{0x74}};
    constexpr std::array<std::byte, 8> kDestroy{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}};
    g_initialize_path = {};
    g_destroy_path = {};
    if (image_base == 0
        || !SignatureMatches(image_base + 0x2ed1370, kInit)
        || !SignatureMatches(image_base + 0x2ed6a80, kDestroy))
    {
        return false;
    }
    g_initialize_path = reinterpret_cast<InitPathFn>(image_base + 0x2ed1370);
    g_destroy_path = reinterpret_cast<DestroyPathFn>(image_base + 0x2ed6a80);
    return true;
}

NavigationState ReplaySceneNavigator::Tick(
    bool playback_context_staged,
    bool require_replay_list,
    std::string& detail)
{
    RC::Unreal::UObject* manager = FindManager();
    RC::Unreal::UObject* scene = CurrentScene(manager);
    if (manager == nullptr || scene == nullptr)
    {
        detail = "game_flow_context_unavailable";
        return NavigationState::Waiting;
    }
    const std::string scene_name = RC::to_string(scene->GetClassPrivate()->GetName());
    if (scene_name.find("ReplayBattleScene") != std::string::npos
        && !require_replay_list)
    {
        detail = scene_name;
        return NavigationState::Ready;
    }
    if (scene_name.find("InitScene") != std::string::npos
        || scene_name.find("AdvertiseScene") != std::string::npos)
    {
        retry_frames_ = 0;
        detail = scene_name;
        return NavigationState::Waiting;
    }
    if (scene_name != last_scene_)
    {
        last_scene_ = scene_name;
        retry_frames_ = 0;
        title_top_requested_ = false;
        title_decide_stage_ = 0;
    }
    if (scene_name.find("TitleScene") != std::string::npos)
    {
        // The cooked startup graph creates RefTitleMenu and MainBehavior before
        // ReadyToStart. Transitioning earlier tears down partially initialized
        // title state. Once both owners exist, take the exact Movie-to-Top state
        // edge used by TitleMovieState's SkipMovie path. The state code becomes
        // visible before Top's cooked OnEntry graph necessarily registers its
        // OnDecidedMainUser delegate. Once the exact CurrentState object is
        // stable, invoke Top's own RequestDecideMainUser custom event to bind
        // that delegate and arm native capture, then force logical user zero
        // through the native manager API. That API normally invokes the
        // registered callback synchronously. If Steam identity readiness makes
        // the weak delegate a no-op, invoke its exact cooked target only after
        // Top remains active for two seconds; both paths preserve the owned
        // sign-in/StartUp graph.
        RC::Unreal::UObject* behavior = ObjectProperty(scene, L"MainBehavior");
        if (ObjectProperty(scene, L"RefTitleMenu") == nullptr
            || behavior == nullptr)
        {
            detail = "title_startup_pending";
            return NavigationState::Waiting;
        }
        if (!title_top_requested_)
        {
            if (!CallStringParam(behavior, L"ChangeState", L"Top"))
            {
                detail = "title_top_failed";
                return NavigationState::Failed;
            }
            title_top_requested_ = true;
        }
        RC::Unreal::UObject* machine = ObjectProperty(behavior, L"Machine");
        const std::string state = StringProperty(machine, L"CurrentStateCode");
        if (state == "Top" && ++retry_frames_ >= 8
            && title_decide_stage_ == 0)
        {
            RC::Unreal::UObject* current_state =
                ObjectProperty(machine, L"CurrentState");
            if (!CallNoParam(current_state, L"RequestDecideMainUser"))
            {
                detail = "title_decide_rearm_pending";
                return NavigationState::Waiting;
            }
            title_decide_stage_ = 1;
            retry_frames_ = 0;
            detail = "title_state:Top:decide_rearmed";
            return NavigationState::Waiting;
        }
        if (state == "Top" && retry_frames_ >= 2
            && title_decide_stage_ == 1)
        {
            RC::Unreal::UObject* signin_manager =
                ObjectProperty(manager, L"SigninManager");
            if (signin_manager == nullptr)
            {
                detail = "title_state:Top:signin_pending";
                return NavigationState::Waiting;
            }
            if (!CallIntParam(signin_manager, L"RequestDecideMainUser", 0))
            {
                detail = "title_user_force_failed";
                return NavigationState::Failed;
            }
            title_decide_stage_ = 2;
            retry_frames_ = 0;
            detail = "title_state:Top:user_forced";
            return NavigationState::Waiting;
        }
        if (state == "Top" && retry_frames_ >= 8
            && title_decide_stage_ == 2)
        {
            RC::Unreal::UObject* current_state =
                ObjectProperty(machine, L"CurrentState");
            if (!CallNoParam(current_state, L"OnDecidedMainUser"))
            {
                detail = "title_decide_callback_pending";
                return NavigationState::Waiting;
            }
            title_decide_stage_ = 3;
            retry_frames_ = 0;
            detail = "title_state:Top:callback_forced";
            return NavigationState::Waiting;
        }
        if (state == "Top" && retry_frames_ >= 8
            && title_decide_stage_ == 3)
        {
            RC::Unreal::UObject* current_state =
                ObjectProperty(machine, L"CurrentState");
            if (!CallNoParam(current_state, L"OnDecidedTitle"))
            {
                detail = "title_decided_callback_pending";
                return NavigationState::Waiting;
            }
            title_decide_stage_ = 4;
            retry_frames_ = 0;
            detail = "title_state:Top:decided_forced";
            return NavigationState::Waiting;
        }
        if (state == "Top" && retry_frames_ >= 8
            && title_decide_stage_ == 4)
        {
            RC::Unreal::UObject* current_state =
                ObjectProperty(machine, L"CurrentState");
            if (!CallNoParam(current_state, L"FinishFadeout"))
            {
                detail = "title_fade_callback_pending";
                return NavigationState::Waiting;
            }
            title_decide_stage_ = 5;
            retry_frames_ = 0;
            detail = "title_state:Top:fade_forced";
            return NavigationState::Waiting;
        }
        if (state == "Top" && retry_frames_ >= 32
            && title_decide_stage_ == 5)
        {
            title_decide_stage_ = 0;
            retry_frames_ = 0;
            detail = "title_state:Top:decide_retry";
            return NavigationState::Waiting;
        }
        detail = state.empty() ? "title_top_requested" : "title_state:" + state;
        return NavigationState::Waiting;
    }
    if (++retry_frames_ < 15)
    {
        detail = scene_name;
        return NavigationState::Waiting;
    }
    retry_frames_ = 0;
    if (scene_name.find("ReplaySetupScene") != std::string::npos)
    {
        detail = scene_name;
        return NavigationState::Waiting;
    }
    if (scene_name.find("ReplayBattleScene") != std::string::npos)
    {
        // ReplayBattleScene's cooked LuxPauseMenu::GoBackToReplaySelect path
        // calls its inherited Blueprint RequestChangeScene custom event. That
        // event installs the battle-scene pre-transition callback before it
        // delegates to ChangeScene, which is required for orderly battle
        // teardown. A direct manager ChangeScene request is ignored here.
        if (!RequestReplayList(scene))
        {
            detail = "request_replay_list_failed:" + scene_name;
            return NavigationState::Failed;
        }
        detail = "request_replay_list:" + scene_name;
        return NavigationState::Waiting;
    }
    if (scene_name.find("ReplayListScene") != std::string::npos
        && !playback_context_staged)
    {
        detail = scene_name;
        return NavigationState::ReplayListReady;
    }
    const wchar_t* tag = scene_name.find("ReplayListScene") != std::string::npos
        ? L"battlesetup" : L"replay_list";
    if (!ChangeScene(manager, tag))
    {
        detail = "change_scene_failed:" + scene_name;
        return NavigationState::Failed;
    }
    detail = "change_scene_requested:" + scene_name;
    return NavigationState::Waiting;
}
}
