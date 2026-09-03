#include "OnlineRoomAutomation.hpp"
#include "deterministic/Sc6OnlineContractObserver.hpp"

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
#include <polyhook2/Detour/x64Detour.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string_view>
#include <vector>

namespace Horse::Qualification
{
namespace
{
using DestroyPathFn = void (__fastcall*)(void*);
DestroyPathFn g_destroy_path{};

bool SignatureMatches(std::uintptr_t address,
                      const std::array<std::byte, 8>& expected) noexcept;

using QueueReadyChannelStateFn = void (__fastcall*)(void*, std::uint8_t);
std::unique_ptr<PLH::x64Detour> g_queue_ready_channel_detour{};
std::uint64_t g_queue_ready_channel_trampoline{};
std::atomic<void*> g_observed_ready_active{};
std::atomic<std::uint8_t> g_observed_ready_state{};
std::atomic<std::uint64_t> g_ready_observation_generation{};

using BattleSyncReceiveFn = void (__fastcall*)(void*, std::uint8_t, void*);
std::unique_ptr<PLH::x64Detour> g_battle_sync_receive_detour{};
std::uint64_t g_battle_sync_receive_trampoline{};

void __fastcall ObserveQueueReadyChannelState(
    void* active_connect, std::uint8_t requested_state) noexcept
{
    g_observed_ready_active.store(active_connect, std::memory_order_relaxed);
    g_observed_ready_state.store(requested_state, std::memory_order_relaxed);
    g_ready_observation_generation.fetch_add(1, std::memory_order_release);
    reinterpret_cast<QueueReadyChannelStateFn>(
        g_queue_ready_channel_trampoline)(active_connect, requested_state);
}

bool InstallReadyChannelObserver(std::uintptr_t image_base) noexcept
{
    constexpr std::array<std::byte, 8> kQueueReady{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x18}, std::byte{0x48}, std::byte{0x89}, std::byte{0x7c}};
    if (g_queue_ready_channel_detour) return true;
    const auto target = image_base + 0x2e5d930;
    if (!SignatureMatches(target, kQueueReady)) return false;
    g_queue_ready_channel_trampoline = 0;
    auto detour = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(target),
        reinterpret_cast<std::uint64_t>(&ObserveQueueReadyChannelState),
        &g_queue_ready_channel_trampoline);
    if (!detour->hook() || g_queue_ready_channel_trampoline == 0)
        return false;
    g_queue_ready_channel_detour = std::move(detour);
    return true;
}

void __fastcall ObserveBattleSyncReceive(
    void* battle_sync, std::uint8_t message_type, void* archive) noexcept;

bool InstallBattleSyncReceiveObserver(std::uintptr_t image_base) noexcept
{
    // LuxOnlineBattleSync_OnRecvBattleSync_Dispatcher. Its first parameter is
    // the exact 0x1BD0 match-data object populated by channel-6 messages.
    constexpr std::array<std::byte, 8> kReceive{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x4c}, std::byte{0x24},
        std::byte{0x08}, std::byte{0x55}, std::byte{0x57}, std::byte{0x41}};
    if (g_battle_sync_receive_detour) return true;
    const auto target = image_base + 0x511cf0;
    if (!SignatureMatches(target, kReceive)) return false;
    g_battle_sync_receive_trampoline = 0;
    auto detour = std::make_unique<PLH::x64Detour>(
        static_cast<std::uint64_t>(target),
        reinterpret_cast<std::uint64_t>(&ObserveBattleSyncReceive),
        &g_battle_sync_receive_trampoline);
    if (!detour->hook() || g_battle_sync_receive_trampoline == 0)
        return false;
    g_battle_sync_receive_detour = std::move(detour);
    return true;
}

struct UIDataObject
{
    void* vtable{};
    void* node_ref{};
    void* reference_count{};
};
static_assert(sizeof(UIDataObject) == 0x18);

bool SignatureMatches(std::uintptr_t address,
                      const std::array<std::byte, 8>& expected) noexcept
{
    __try
    {
        return std::memcmp(reinterpret_cast<void*>(address), expected.data(),
                           expected.size()) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadBattleSyncFlags(void* sync, std::uint8_t& profile,
                         std::uint8_t& character,
                         std::uint8_t& stage) noexcept
{
    __try
    {
        const auto* bytes = static_cast<const std::byte*>(sync);
        profile = *reinterpret_cast<const std::uint8_t*>(bytes + 0x1bcd);
        character = *reinterpret_cast<const std::uint8_t*>(bytes + 0x1bce);
        stage = *reinterpret_cast<const std::uint8_t*>(bytes + 0x1bcf);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct BattleSyncRawContent
{
    std::int32_t fighter_count[2]{};
    std::int32_t fighter_capacity[2]{};
    char fighter_text[2][17]{};
    std::uint16_t stage{};
    std::uint8_t random{};
};

struct BattleSyncWideString
{
    const wchar_t* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};
static_assert(sizeof(BattleSyncWideString) == 0x10);

bool ReadBattleSyncRawContent(void* sync, BattleSyncRawContent& output) noexcept
{
    __try
    {
        const auto* bytes = static_cast<const std::byte*>(sync);
        for (std::size_t player = 0; player < 2; ++player)
        {
            const auto* text = reinterpret_cast<const BattleSyncWideString*>(
                bytes + 0x2e0 + player * 0xc70 + 0xc58);
            output.fighter_count[player] = text->count;
            output.fighter_capacity[player] = text->capacity;
            if (text->data == nullptr || text->count <= 0) continue;
            const auto maximum = (std::min)(text->count, 16);
            for (std::int32_t index = 0; index < maximum; ++index)
            {
                const wchar_t value = text->data[index];
                if (value == L'\0') break;
                output.fighter_text[player][index] = value <= 0x7f
                    ? static_cast<char>(value) : '?';
            }
        }
        output.stage = *reinterpret_cast<const std::uint16_t*>(
            bytes + 0x1bc0);
        output.random = *reinterpret_cast<const std::uint8_t*>(
            bytes + 0x1bc4);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct BattleSyncObservation
{
    bool present{};
    bool detailed_valid{};
    Horse::Deterministic::Sc6BattleSyncIdentity detailed{};
    bool flags_readable{};
    std::uint8_t profile_received{};
    std::uint8_t characters_received{};
    std::uint8_t stage_received{};
    bool raw_readable{};
    BattleSyncRawContent raw{};
    std::uint64_t generation{};
};

SRWLOCK g_battle_sync_observation_lock = SRWLOCK_INIT;
BattleSyncObservation g_battle_sync_observation{};

void __fastcall ObserveBattleSyncReceive(
    void* battle_sync, std::uint8_t message_type, void* archive) noexcept
{
    reinterpret_cast<BattleSyncReceiveFn>(
        g_battle_sync_receive_trampoline)(battle_sync, message_type, archive);
    if (battle_sync == nullptr || message_type != 6) return;

    // The stock match-data object is scene-owned and may be destroyed or
    // reused before the qualification tick polls it. Copy the bounded native
    // contract while the receiver still guarantees the object is live.
    BattleSyncObservation observation{};
    observation.present = true;
    const Horse::Deterministic::Sc6BattleSyncObserver observer{};
    observation.detailed_valid = observer.ObserveDetailed(
        battle_sync, observation.detailed).ok();
    observation.flags_readable = ReadBattleSyncFlags(
        battle_sync, observation.profile_received,
        observation.characters_received, observation.stage_received);
    observation.raw_readable = ReadBattleSyncRawContent(
        battle_sync, observation.raw);
    AcquireSRWLockExclusive(&g_battle_sync_observation_lock);
    observation.generation = g_battle_sync_observation.generation + 1;
    g_battle_sync_observation = observation;
    ReleaseSRWLockExclusive(&g_battle_sync_observation_lock);
}

BattleSyncObservation ReadBattleSyncObservation() noexcept
{
    AcquireSRWLockShared(&g_battle_sync_observation_lock);
    const auto observation = g_battle_sync_observation;
    ReleaseSRWLockShared(&g_battle_sync_observation_lock);
    return observation;
}

void ResetBattleSyncObservation() noexcept
{
    AcquireSRWLockExclusive(&g_battle_sync_observation_lock);
    g_battle_sync_observation = {};
    ReleaseSRWLockExclusive(&g_battle_sync_observation_lock);
}

bool IsReal(RC::Unreal::UObject* object) noexcept
{
    return object != nullptr && RC::Unreal::UObject::IsReal(object);
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

RC::Unreal::UObject* ObjectProperty(RC::Unreal::UObject* owner,
                                    const wchar_t* name) noexcept
{
    if (!IsReal(owner)) return nullptr;
    try
    {
        auto* property = owner->GetPropertyByNameInChain(name);
        if (property == nullptr || property->GetSize() < sizeof(void*))
            return nullptr;
        void* value = property->ContainerPtrToValuePtr<void>(owner);
        auto* object = value != nullptr
            ? *reinterpret_cast<RC::Unreal::UObject**>(value) : nullptr;
        return IsReal(object) ? object : nullptr;
    }
    catch (...) { return nullptr; }
}

std::string ClassName(RC::Unreal::UObject* object) noexcept
{
    if (!IsReal(object)) return {};
    try
    {
        auto* type = object->GetClassPrivate();
        return type != nullptr ? RC::to_string(type->GetName()) : std::string{};
    }
    catch (...) { return {}; }
}

RC::Unreal::UObject* FindManager() noexcept
{
    auto* manager =
        RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxUIGameFlowManager");
    return IsReal(manager) ? manager : nullptr;
}

RC::Unreal::UObject* CurrentScene(RC::Unreal::UObject* manager) noexcept
{
    return ObjectProperty(manager, L"CurrentScene");
}

RC::Unreal::UObject* CurrentLobbyState(RC::Unreal::UObject* scene) noexcept
{
    return ObjectProperty(ObjectProperty(ObjectProperty(scene, L"RootBehavior"),
                                         L"Machine"),
                          L"CurrentState");
}

bool IdentityContains(RC::Unreal::UObject* object, const char* fragment) noexcept
{
    if (!IsReal(object) || fragment == nullptr) return false;
    const std::string class_name = ClassName(object);
    if (class_name.find(fragment) != std::string::npos) return true;
    try { return RC::to_string(object->GetName()).find(fragment) != std::string::npos; }
    catch (...) { return false; }
}

bool CallNoParam(RC::Unreal::UObject* owner, const wchar_t* name) noexcept
{
    if (!IsReal(owner)) return false;
    try
    {
        auto* function = owner->GetFunctionByNameInChain(name);
        if (function == nullptr) return false;
        owner->ProcessEvent(function, nullptr);
        return true;
    }
    catch (...) { return false; }
}

bool CallStringParam(RC::Unreal::UObject* owner, const wchar_t* name,
                     const wchar_t* value) noexcept
{
    if (!IsReal(owner)) return false;
    try
    {
        auto* function = owner->GetFunctionByNameInChain(name);
        if (function == nullptr) return false;
        struct Params { RC::Unreal::FString value; } params{
            RC::Unreal::FString(value)};
        owner->ProcessEvent(function, &params);
        return true;
    }
    catch (...) { return false; }
}

bool CallIntParam(RC::Unreal::UObject* owner, const wchar_t* name,
                  std::int32_t value) noexcept
{
    if (!IsReal(owner)) return false;
    try
    {
        auto* function = owner->GetFunctionByNameInChain(name);
        if (function == nullptr) return false;
        struct Params { std::int32_t value; } params{value};
        owner->ProcessEvent(function, &params);
        return true;
    }
    catch (...) { return false; }
}

std::string StringProperty(RC::Unreal::UObject* owner,
                           const wchar_t* name) noexcept
{
    if (!IsReal(owner)) return {};
    try
    {
        auto* value = owner->GetValuePtrByPropertyNameInChain<
            RC::Unreal::FString>(name);
        return value != nullptr ? RC::to_string(**value) : std::string{};
    }
    catch (...) { return {}; }
}

void Destroy(UIDataObject& value) noexcept
{
    if (g_destroy_path != nullptr) g_destroy_path(&value);
    value = {};
}

bool CreateDataObject(const wchar_t* function_name, const wchar_t* param_name,
                      const wchar_t* value, UIDataObject& output) noexcept
{
    output = {};
    auto* cdo = RC::Unreal::UObjectGlobals::StaticFindObject<
        RC::Unreal::UObject*>(nullptr, nullptr,
        STR("/Script/UMGUtil.Default__UMGUtilUIDataObjectLibrary"));
    if (!IsReal(cdo)) return false;
    auto* function = cdo->GetFunctionByNameInChain(function_name);
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096)
        return false;
    auto* input = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, param_name));
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

bool EnterPlayerMatch(RC::Unreal::UObject* scene) noexcept
{
    if (!IsReal(scene)) return false;
    // MainMenuScene's cooked requestChangeScene owns the online availability
    // check and queues PlayerMatchLobbyScene. The older harness used this
    // route when the generic OnCommand event was accepted without a scene
    // transition, which this build reproduces.
    if (CallStringParam(scene, L"requestChangeScene", L"playermatch"))
        return true;
    auto* function = scene->GetFunctionByNameInChain(L"OnCommand");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096)
        return false;
    auto* event = Param(function, L"EventData");
    if (event == nullptr || event->GetSize() <= 0
        || event->GetSize() > static_cast<std::int32_t>(sizeof(UIDataObject)))
        return false;
    UIDataObject data{};
    if (!CreateDataObject(L"Parse", L"JsonStringToParse",
                          L"{\"command\":\"playermatch\"}", data))
    {
        if (!CreateDataObject(L"Parse", L"JsonString",
                              L"{\"command\":\"playermatch\"}", data))
            return false;
    }
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    void* slot = event->ContainerPtrToValuePtr<void>(params.data());
    if (slot == nullptr) { Destroy(data); return false; }
    std::memcpy(slot, &data, static_cast<std::size_t>(event->GetSize()));
    try { scene->ProcessEvent(function, params.data()); }
    catch (...) { Destroy(data); return false; }
    Destroy(data);
    return true;
}

bool RequestPlayerMatchLobbyTransition(
    RC::Unreal::UObject* scene) noexcept
{
    if (!IsReal(scene)) return false;
    auto* function = scene->GetFunctionByNameInChain(L"RequestChangeScene");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096)
        return false;
    auto* tag = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, L"inTag"));
    if (tag == nullptr)
        tag = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
            Param(function, L"InTag"));
    auto* inherited = Param(function, L"inInheritedData");
    if (inherited == nullptr) inherited = Param(function, L"InInheritedData");
    if (tag == nullptr || inherited == nullptr
        || inherited->GetSize() <= 0
        || inherited->GetSize()
            > static_cast<std::int32_t>(sizeof(UIDataObject)))
        return false;

    UIDataObject data{};
    if (!CreateDataObject(L"Conv_StringToUIDataObject", L"inString",
            L"InRoom", data)
        && !CreateDataObject(L"Conv_StringToUIDataObject", L"InString",
            L"InRoom", data))
        return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    tag->InitializeValue_InContainer(params.data());
    tag->SetPropertyValueInContainer(
        params.data(), RC::Unreal::FString(L"battlelobby"));
    void* inherited_slot =
        inherited->ContainerPtrToValuePtr<void>(params.data());
    if (inherited_slot == nullptr)
    {
        tag->DestroyValue_InContainer(params.data());
        Destroy(data);
        return false;
    }
    std::memcpy(inherited_slot, &data,
        static_cast<std::size_t>(inherited->GetSize()));
    try { scene->ProcessEvent(function, params.data()); }
    catch (...)
    {
        tag->DestroyValue_InContainer(params.data());
        Destroy(data);
        return false;
    }
    tag->DestroyValue_InContainer(params.data());
    Destroy(data);
    return true;
}

RC::Unreal::FProperty* FirstParam(
    RC::Unreal::UFunction* function,
    std::initializer_list<const wchar_t*> names) noexcept
{
    for (const wchar_t* name : names)
        if (auto* property = Param(function, name)) return property;
    return nullptr;
}

bool WriteObjectParam(RC::Unreal::FProperty* property, void* params,
                      RC::Unreal::UObject* value) noexcept
{
    if (property == nullptr || property->GetSize() < sizeof(void*)) return false;
    auto** slot = reinterpret_cast<RC::Unreal::UObject**>(
        property->ContainerPtrToValuePtr<void>(params));
    if (slot == nullptr) return false;
    *slot = value;
    return true;
}

bool ResolveMenuItem(RC::Unreal::UObject* list, const wchar_t* menu_id,
                     RC::Unreal::UObject*& item,
                     std::int32_t& resolved_index) noexcept
{
    item = nullptr;
    resolved_index = -1;
    if (!IsReal(list)) return false;
    auto* get_index = list->GetFunctionByNameInChain(L"GetIndexFromMenuID");
    if (get_index == nullptr || get_index->GetPropertiesSize() <= 0
        || get_index->GetPropertiesSize() > 1024) return false;
    auto* id = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        FirstParam(get_index, {L"InMenuID", L"MenuID", L"MenuId"}));
    auto* index = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        FirstParam(get_index, {L"Index", L"OutIndex", L"ReturnValue"}));
    if (id == nullptr || index == nullptr) return false;
    std::vector<std::uint8_t> index_params(
        static_cast<std::size_t>(get_index->GetPropertiesSize()), 0);
    id->InitializeValue_InContainer(index_params.data());
    id->SetPropertyValueInContainer(index_params.data(),
                                     RC::Unreal::FString(menu_id));
    try { list->ProcessEvent(get_index, index_params.data()); }
    catch (...) { id->DestroyValue_InContainer(index_params.data()); return false; }
    auto* index_slot = index->ContainerPtrToValuePtr<std::int32_t>(
        index_params.data());
    resolved_index = index_slot != nullptr ? *index_slot : -1;
    id->DestroyValue_InContainer(index_params.data());
    if (resolved_index < 0) return false;

    auto* get_item = list->GetFunctionByNameInChain(L"GetItem");
    if (get_item == nullptr || get_item->GetPropertiesSize() <= 0
        || get_item->GetPropertiesSize() > 1024) return false;
    auto* item_index = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        FirstParam(get_item, {L"InIndex", L"Index", L"SelectedIndex",
                              L"InFocusIndex", L"ItemIndex"}));
    auto* item_result = FirstParam(
        get_item, {L"Item", L"MainMenuListItem", L"ReturnValue", L"GetItem",
                   L"FocusItem", L"AsMain Menu List Item", L"OutItem"});
    if (item_index == nullptr || item_result == nullptr) return false;
    std::vector<std::uint8_t> item_params(
        static_cast<std::size_t>(get_item->GetPropertiesSize()), 0);
    *item_index->ContainerPtrToValuePtr<std::int32_t>(item_params.data()) =
        resolved_index;
    try { list->ProcessEvent(get_item, item_params.data()); }
    catch (...) { return false; }
    auto** item_slot = reinterpret_cast<RC::Unreal::UObject**>(
        item_result->ContainerPtrToValuePtr<void>(item_params.data()));
    item = item_slot != nullptr ? *item_slot : nullptr;
    return IsReal(item);
}

bool FocusMenuItem(RC::Unreal::UObject* list,
                   RC::Unreal::UObject* item,
                   std::int32_t index) noexcept
{
    auto* function = IsReal(list)
        ? list->GetFunctionByNameInChain(L"FocusItem") : nullptr;
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 1024) return false;
    auto* input = FirstParam(function,
        {L"MainMenuListItem", L"Item", L"FocusItem", L"InItem"});
    if (input != nullptr)
    {
        std::vector<std::uint8_t> params(
            static_cast<std::size_t>(function->GetPropertiesSize()), 0);
        if (WriteObjectParam(input, params.data(), item))
        {
            try { list->ProcessEvent(function, params.data()); return true; }
            catch (...) {}
        }
    }
    auto* integer = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        FirstParam(function, {L"Index", L"InIndex", L"ItemIndex"}));
    if (integer != nullptr && index >= 0)
    {
        std::vector<std::uint8_t> params(
            static_cast<std::size_t>(function->GetPropertiesSize()), 0);
        *integer->ContainerPtrToValuePtr<std::int32_t>(params.data()) = index;
        try { list->ProcessEvent(function, params.data()); return true; }
        catch (...) {}
    }
    auto* select = list->GetFunctionByNameInChain(L"SelectIndex");
    if (select == nullptr || select->GetPropertiesSize() <= 0
        || select->GetPropertiesSize() > 1024) return false;
    integer = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        FirstParam(select, {L"Index", L"InIndex", L"ItemIndex"}));
    if (integer == nullptr || index < 0) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(select->GetPropertiesSize()), 0);
    *integer->ContainerPtrToValuePtr<std::int32_t>(params.data()) = index;
    try { list->ProcessEvent(select, params.data()); return true; }
    catch (...) { return false; }
}

bool DecideMenuItem(RC::Unreal::UObject* impl,
                    RC::Unreal::UObject* item) noexcept
{
    if (!IsReal(impl) || !IsReal(item)) return false;
    auto* function = impl->GetFunctionByNameInChain(L"OnDecide(Item)");
    if (function == nullptr) function = impl->GetFunctionByNameInChain(L"OnDecide");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 1024) return false;
    auto* input = FirstParam(function, {L"MainMenuListItem", L"Item"});
    if (input == nullptr) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    if (!WriteObjectParam(input, params.data(), item)) return false;
    try { impl->ProcessEvent(function, params.data()); return true; }
    catch (...) { return false; }
}

bool SemanticMainMenuAction(RC::Unreal::UObject* scene,
                            const wchar_t* menu_id, bool decide,
                            std::string& detail) noexcept
{
    auto* ref_menu = ObjectProperty(scene, L"RefMainMenu");
    auto* impl = ObjectProperty(ref_menu, L"UIWidgetImpl");
    if (!IsReal(impl))
        impl = ObjectProperty(ObjectProperty(scene, L"MMenu"), L"UIWidgetImpl");
    if (!IsReal(impl))
        impl = RC::Unreal::UObjectGlobals::FindFirstOf(L"BP_MainMenuImpl_C");
    if (!IsReal(impl)) { detail = "impl_unavailable"; return false; }
    const std::array<const wchar_t*, 3> names =
        menu_id != nullptr && std::wcscmp(menu_id, L"EPLAYERMATCH") == 0
        ? std::array<const wchar_t*, 3>{L"SubList", L"TargetList", L"MainList"}
        : std::array<const wchar_t*, 3>{L"TargetList", L"MainList", L"SubList"};
    for (const wchar_t* name : names)
    {
        auto* list = ObjectProperty(impl, name);
        const std::string wrapper_class = ClassName(list);
        auto* list_impl = ObjectProperty(list, L"UIWidgetImpl");
        if (IsReal(list_impl)) list = list_impl;
        RC::Unreal::UObject* item{};
        std::int32_t index{-1};
        if (IsReal(list) && ResolveMenuItem(list, menu_id, item, index)
            && FocusMenuItem(list, item, index)
            && (!decide || DecideMenuItem(impl, item)))
        { detail = "ok"; return true; }
        detail += RC::to_string(name);
        detail += "=" + wrapper_class + "/" + ClassName(list);
        if (IsReal(list))
        {
            detail += list->GetFunctionByNameInChain(L"GetIndexFromMenuID")
                ? ":getindex" : ":no-getindex";
            detail += list->GetFunctionByNameInChain(L"GetItem")
                ? ":getitem" : ":no-getitem";
        }
        detail += ";";
    }
    return false;
}

bool RequestLobbyState(RC::Unreal::UObject* scene,
                       const wchar_t* state) noexcept
{
    return CallStringParam(scene, L"RequestChangeState", state);
}

bool SendCreateCommand(RC::Unreal::UObject* scene,
                       RC::Unreal::UObject* state) noexcept
{
    if (!IsReal(scene) || !IsReal(state)) return false;
    auto* function = state->GetFunctionByNameInChain(L"OnRequestInputCommand");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096)
        return false;
    auto* menu = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, L"MenuName"));
    auto* command = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, L"CommandName"));
    auto* controller = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        Param(function, L"ControllerId"));
    auto* param = Param(function, L"Param");
    if (menu == nullptr || command == nullptr || controller == nullptr
        || param == nullptr || param->GetSize() <= 0
        || param->GetSize() > static_cast<std::int32_t>(sizeof(UIDataObject)))
        return false;
    UIDataObject data{};
    if (!CreateDataObject(L"Conv_StringToUIDataObject", L"inString",
                          L"StartCreateRoom", data)
        && !CreateDataObject(L"Conv_StringToUIDataObject", L"InString",
                             L"StartCreateRoom", data))
        return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    bool menu_initialized{};
    bool command_initialized{};
    try
    {
        menu->InitializeValue_InContainer(params.data());
        menu_initialized = true;
        command->InitializeValue_InContainer(params.data());
        command_initialized = true;
        menu->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(L"PlayerMatchRoomCreationWindow"));
        command->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(L"Decide"));
        *controller->ContainerPtrToValuePtr<std::int32_t>(params.data()) = -1;
        std::memcpy(param->ContainerPtrToValuePtr<void>(params.data()), &data,
                    static_cast<std::size_t>(param->GetSize()));
        auto set_object = [&](const wchar_t* name, RC::Unreal::UObject* value)
        {
            auto* property = Param(function, name);
            if (property == nullptr) return;
            auto** slot = reinterpret_cast<RC::Unreal::UObject**>(
                property->ContainerPtrToValuePtr<void>(params.data()));
            if (slot != nullptr) *slot = value;
        };
        set_object(L"TargetWidget", state);
        set_object(L"GameFlowScene", scene);
        state->ProcessEvent(function, params.data());
        Destroy(data);
        command->DestroyValue_InContainer(params.data());
        menu->DestroyValue_InContainer(params.data());
        return true;
    }
    catch (...)
    {
        Destroy(data);
        if (command_initialized) command->DestroyValue_InContainer(params.data());
        if (menu_initialized) menu->DestroyValue_InContainer(params.data());
        return false;
    }
}

struct SharedSession
{
    void* object{};
    void* controller{};
};

void ReleaseSharedSession(SharedSession& value) noexcept
{
    auto* controller = static_cast<std::byte*>(value.controller);
    value = {};
    if (controller == nullptr) return;
    auto* strong = reinterpret_cast<volatile long*>(controller + 8);
    if (InterlockedDecrement(strong) != 0) return;
    auto** vtable = *reinterpret_cast<void***>(controller);
    reinterpret_cast<void (*)(void*)>(vtable[0])(controller);
    auto* weak = reinterpret_cast<volatile long*>(controller + 12);
    if (InterlockedDecrement(weak) == 0)
        reinterpret_cast<void (*)(void*, int)>(vtable[1])(controller, 1);
}

bool ObserveLocalLobby(std::uintptr_t image_base, std::uint64_t& lobby_id,
                       std::uint8_t* role = nullptr) noexcept
{
    lobby_id = 0;
    SharedSession retained{};
    bool ok{};
    __try
    {
        using AcquireFn = SharedSession* (*)(SharedSession*);
        reinterpret_cast<AcquireFn>(image_base + 0x003f07a0)(&retained);
        if (retained.object != nullptr && retained.controller != nullptr)
        {
            auto* session = static_cast<std::byte*>(retained.object);
            auto** vtable = *reinterpret_cast<void***>(session);
            if (vtable == reinterpret_cast<void**>(image_base + 0x03d27940))
            {
                const auto observed_role = reinterpret_cast<std::int8_t (*)(void*)>(
                    vtable[0])(session);
                auto* active = session - 0x18;
                auto* online_session = *reinterpret_cast<void**>(active + 0x118);
                const auto session_name = *reinterpret_cast<std::uint64_t*>(
                    active + 0x30);
                if ((observed_role == 0 || observed_role == 1)
                    && online_session != nullptr && session_name != 0)
                {
                    auto** online_vtable = *reinterpret_cast<void***>(online_session);
                    using GetNamedFn = void* (*)(void*, const std::uint64_t*);
                    auto* named = static_cast<std::byte*>(
                        reinterpret_cast<GetNamedFn>(online_vtable[3])(
                            online_session, &session_name));
                    auto* info = named != nullptr
                        ? *reinterpret_cast<std::byte**>(named + 0xa8) : nullptr;
                    if (info != nullptr)
                    {
                        lobby_id = *reinterpret_cast<std::uint64_t*>(info + 0x48);
                        ok = lobby_id != 0;
                        if (role != nullptr)
                            *role = static_cast<std::uint8_t>(observed_role);
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    ReleaseSharedSession(retained);
    return ok;
}

bool ObserveNamedLobby(std::uintptr_t image_base,
                       std::uint64_t& lobby_id) noexcept
{
    lobby_id = 0;
    SharedSession shared{};
    bool ok{};
    __try
    {
        using ResolveFn = void* (__fastcall*)(SharedSession*, std::uint64_t);
        reinterpret_cast<ResolveFn>(image_base + 0x2ea0470)(&shared, 0);
        if (shared.object != nullptr && shared.controller != nullptr)
        {
            auto** vtable = *reinterpret_cast<void***>(shared.object);
            using GetNamedFn = void* (__fastcall*)(void*, std::uint64_t);
            RC::Unreal::FName session_name(
                L"PlayerMatch", RC::Unreal::FNAME_Find);
            if (session_name.ToUnstableInt() == 0)
                session_name = RC::Unreal::FName(
                    L"PlayerMatch", RC::Unreal::FNAME_Add);
            const std::uint64_t raw_name = session_name.ToUnstableInt();
            auto* named = static_cast<std::byte*>(
                reinterpret_cast<GetNamedFn>(vtable[3])(
                    shared.object, raw_name));
            auto* info = named != nullptr
                ? *reinterpret_cast<std::byte**>(named + 0xa8) : nullptr;
            if (info != nullptr)
            {
                lobby_id = *reinterpret_cast<std::uint64_t*>(info + 0x48);
                ok = lobby_id != 0;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    ReleaseSharedSession(shared);
    return ok;
}

bool ObserveAnyLobby(std::uintptr_t image_base,
                     std::uint64_t& lobby_id) noexcept
{
    return ObserveLocalLobby(image_base, lobby_id)
        || ObserveNamedLobby(image_base, lobby_id);
}

struct SteamBindings
{
    using SteamClientFn = void* (__cdecl*)();
    using GetHandleFn = int (__cdecl*)();
    using GetInterfaceFn = void* (__cdecl*)(void*, int, int, const char*);
    using GetSteamIdFn = std::uint64_t (__cdecl*)(void*);
    using LobbyIntFn = int (__cdecl*)(void*, std::uint64_t);
    using LobbyIdFn = std::uint64_t (__cdecl*)(void*, std::uint64_t);
    using LobbyBoolFn = bool (__cdecl*)(void*, std::uint64_t);
    using LobbyDataFn = const char* (__cdecl*)(void*, std::uint64_t, const char*);
    using LobbyMemberDataFn = const char* (__cdecl*)(
        void*, std::uint64_t, std::uint64_t, const char*);
    using SetLobbyMemberDataFn = void (__cdecl*)(
        void*, std::uint64_t, const char*, const char*);
    using SetLobbyTypeFn = bool (__cdecl*)(void*, std::uint64_t, int);

    void* matchmaking{};
    void* user{};
    std::uint64_t local_id{};
    LobbyIntFn data_count{};
    LobbyIntFn member_count{};
    LobbyIdFn owner{};
    LobbyBoolFn request_data{};
    LobbyDataFn data{};
    LobbyMemberDataFn member_data{};
    SetLobbyMemberDataFn set_member_data{};
    SetLobbyTypeFn set_type{};

    bool Initialize() noexcept
    {
        const HMODULE module = GetModuleHandleW(L"steam_api64.dll");
        if (module == nullptr) return false;
        const auto resolve = [module](const char* name) {
            return GetProcAddress(module, name);
        };
        auto steam_client = reinterpret_cast<SteamClientFn>(
            resolve("SteamClient"));
        auto get_user_handle = reinterpret_cast<GetHandleFn>(
            resolve("SteamAPI_GetHSteamUser"));
        auto get_pipe_handle = reinterpret_cast<GetHandleFn>(
            resolve("SteamAPI_GetHSteamPipe"));
        auto get_user = reinterpret_cast<GetInterfaceFn>(
            resolve("SteamAPI_ISteamClient_GetISteamUser"));
        auto get_matchmaking = reinterpret_cast<GetInterfaceFn>(
            resolve("SteamAPI_ISteamClient_GetISteamMatchmaking"));
        auto get_steam_id = reinterpret_cast<GetSteamIdFn>(
            resolve("SteamAPI_ISteamUser_GetSteamID"));
        data_count = reinterpret_cast<LobbyIntFn>(
            resolve("SteamAPI_ISteamMatchmaking_GetLobbyDataCount"));
        member_count = reinterpret_cast<LobbyIntFn>(
            resolve("SteamAPI_ISteamMatchmaking_GetNumLobbyMembers"));
        owner = reinterpret_cast<LobbyIdFn>(
            resolve("SteamAPI_ISteamMatchmaking_GetLobbyOwner"));
        request_data = reinterpret_cast<LobbyBoolFn>(
            resolve("SteamAPI_ISteamMatchmaking_RequestLobbyData"));
        data = reinterpret_cast<LobbyDataFn>(
            resolve("SteamAPI_ISteamMatchmaking_GetLobbyData"));
        member_data = reinterpret_cast<LobbyMemberDataFn>(
            resolve("SteamAPI_ISteamMatchmaking_GetLobbyMemberData"));
        set_member_data = reinterpret_cast<SetLobbyMemberDataFn>(
            resolve("SteamAPI_ISteamMatchmaking_SetLobbyMemberData"));
        set_type = reinterpret_cast<SetLobbyTypeFn>(
            resolve("SteamAPI_ISteamMatchmaking_SetLobbyType"));
        if (!steam_client || !get_user_handle || !get_pipe_handle || !get_user
            || !get_matchmaking || !get_steam_id || !data_count || !member_count || !owner
            || !request_data || !data || !member_data || !set_member_data
            || !set_type)
            return false;
        void* client = steam_client();
        const int user_handle = get_user_handle();
        const int pipe_handle = get_pipe_handle();
        if (!client || user_handle == 0 || pipe_handle == 0) return false;
        user = get_user(client, user_handle, pipe_handle, "SteamUser019");
        matchmaking = get_matchmaking(
            client, user_handle, pipe_handle, "SteamMatchMaking009");
        local_id = user != nullptr ? get_steam_id(user) : 0;
        return matchmaking != nullptr && local_id != 0;
    }

    bool MetadataReady(std::uint64_t lobby) const noexcept
    {
        if (matchmaking == nullptr || data_count(matchmaking, lobby) <= 0)
            return false;
        constexpr std::array<const char*, 5> keys{
            "buildid", "SESSIONFLAGS", "OWNINGID", "P2PADDR", "P2PPORT"};
        return std::all_of(keys.begin(), keys.end(), [&](const char* key) {
            const char* value = data(matchmaking, lobby, key);
            return value != nullptr && value[0] != '\0';
        });
    }
};

struct SteamUniqueIdInline
{
    std::uintptr_t vtable{};
    std::uint64_t reserved0{};
    std::uint64_t reserved1{};
    std::uint64_t steam_id{};
};

struct SteamInviteEvent
{
    std::uintptr_t vtable{};
    std::uint64_t reserved0{};
    void* subsystem{};
    SteamUniqueIdInline friend_id{};
    SteamUniqueIdInline lobby_id{};
    std::uint32_t local_user_num{};
    std::uint32_t reserved1{};
};
static_assert(sizeof(SteamInviteEvent) == 0x60);

bool QueueStockInvite(std::uintptr_t image_base, std::uint64_t lobby_id,
                      std::uint64_t host_id) noexcept
{
    SharedSession shared{};
    using ResolveFn = void* (__fastcall*)(SharedSession*, std::uint64_t);
    using ConstructFn = SteamInviteEvent* (__fastcall*)(SteamInviteEvent*,
        void*, const SteamUniqueIdInline*, const SteamUniqueIdInline*);
    using QueueFn = void (__fastcall*)(void*, void*);
    bool queued{};
    __try
    {
        reinterpret_cast<ResolveFn>(image_base + 0x2ea0470)(&shared, 0);
        if (shared.object == nullptr)
        {
            const RC::Unreal::FName steam(L"STEAM", RC::Unreal::FNAME_Find);
            reinterpret_cast<ResolveFn>(image_base + 0x2ea0470)(
                &shared, steam.ToUnstableInt());
        }
        auto* session = static_cast<std::byte*>(shared.object);
        void* subsystem = session != nullptr
            ? *reinterpret_cast<void**>(session + 0xa20) : nullptr;
        void* manager = subsystem != nullptr
            ? *reinterpret_cast<void**>(static_cast<std::byte*>(subsystem) + 0x280)
            : nullptr;
        using RC::Unreal::GMalloc;
        if (manager != nullptr && GMalloc != nullptr && *GMalloc != nullptr)
        {
            auto* event = static_cast<SteamInviteEvent*>((*GMalloc)->Malloc(
                sizeof(SteamInviteEvent), alignof(SteamInviteEvent)));
            if (event != nullptr)
            {
                std::memset(event, 0, sizeof(*event));
                SteamUniqueIdInline host{
                    image_base + 0x3ba7700, 0, 0, host_id};
                SteamUniqueIdInline lobby{
                    image_base + 0x3ba7700, 0, 0, lobby_id};
                if (reinterpret_cast<ConstructFn>(image_base + 0x29a7ec0)(
                        event, subsystem, &host, &lobby) == event)
                {
                    reinterpret_cast<QueueFn>(image_base + 0x2dc59c0)(
                        manager, event);
                    queued = true;
                }
                else
                {
                    (*GMalloc)->Free(event);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { queued = false; }
    ReleaseSharedSession(shared);
    return queued;
}

bool WriteInteger(RC::Unreal::FProperty* property, void* params,
                  std::int32_t value) noexcept
{
    if (property == nullptr || params == nullptr) return false;
    void* slot = property->ContainerPtrToValuePtr<void>(params);
    if (slot == nullptr) return false;
    if (property->GetSize() == 1)
        *static_cast<std::uint8_t*>(slot) = static_cast<std::uint8_t>(value);
    else if (property->GetSize() == 4)
        *static_cast<std::int32_t*>(slot) = value;
    else return false;
    return true;
}

bool BoolQuery(RC::Unreal::UObject* owner, const wchar_t* name,
               bool& value) noexcept
{
    value = false;
    if (!IsReal(owner)) return false;
    auto* function = owner->GetFunctionByNameInChain(name);
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 512) return false;
    auto* result = RC::Unreal::CastField<RC::Unreal::FBoolProperty>(
        function->GetReturnProperty());
    if (result == nullptr) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    try
    {
        owner->ProcessEvent(function, params.data());
        value = result->GetPropertyValueInContainer(params.data());
        return true;
    }
    catch (...) { return false; }
}

bool SetStringProperty(RC::Unreal::UObject* owner, const wchar_t* name,
                       std::string_view value) noexcept
{
    if (!IsReal(owner)) return false;
    auto* property = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        owner->GetPropertyByNameInChain(name));
    if (property == nullptr) return false;
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0 || needed > 64) return false;
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), wide.data(), needed) != needed)
        return false;
    try
    {
        property->SetPropertyValueInContainer(owner, RC::Unreal::FString(wide));
        return true;
    }
    catch (...) { return false; }
}

std::wstring Widen(std::string_view value)
{
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), needed) != needed)
        return {};
    return result;
}

RC::Unreal::UObject* CurrentSetupState(RC::Unreal::UObject* scene) noexcept
{
    auto* state = ObjectProperty(ObjectProperty(ObjectProperty(
        ObjectProperty(ObjectProperty(scene, L"RootBehavior"), L"Machine"),
        L"CurrentState"), L"SubStateBehavior"), L"Machine");
    state = ObjectProperty(state, L"CurrentState");
    return IdentityContains(state, "PlayerMatchCharaSelectExecState")
        ? state : nullptr;
}

bool ChangeSelectionFocus(RC::Unreal::UObject* widget, std::int32_t index,
                          bool right) noexcept
{
    if (!IsReal(widget)) return false;
    auto* function = widget->GetFunctionByNameInChain(L"ChangeFocus");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 512) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    auto* index_property = FirstParam(function,
        {L"FocusId", L"select_index", L"SelectIndex", L"Index"});
    auto* right_property = RC::Unreal::CastField<RC::Unreal::FBoolProperty>(
        FirstParam(function, {L"bRight", L"Right"}));
    if (!WriteInteger(index_property, params.data(), index)) return false;
    if (right_property != nullptr)
        right_property->SetPropertyValueInContainer(params.data(), right);
    try { widget->ProcessEvent(function, params.data()); return true; }
    catch (...) { return false; }
}

bool CharacterCursor(RC::Unreal::UObject* widget, std::string_view code,
                     std::int32_t& cursor) noexcept
{
    cursor = -1;
    if (!IsReal(widget)) return false;
    auto* function = widget->GetFunctionByNameInChain(
        L"GetCursorIndexByCharaCode");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 512) return false;
    auto* code_property = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        FirstParam(function, {L"CharaCode", L"InCharaCode"}));
    auto* cursor_property = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        FirstParam(function, {L"CursorIndex", L"OutCursorIndex", L"ReturnValue"}));
    if (code_property == nullptr || cursor_property == nullptr) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    const auto wide = Widen(code);
    code_property->InitializeValue_InContainer(params.data());
    try
    {
        code_property->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(wide));
        widget->ProcessEvent(function, params.data());
        cursor = *cursor_property->ContainerPtrToValuePtr<std::int32_t>(
            params.data());
    }
    catch (...) { cursor = -1; }
    code_property->DestroyValue_InContainer(params.data());
    return cursor >= 0;
}

bool SetDecidedPlayer(RC::Unreal::UObject* widget, std::uint8_t side,
                      bool decided) noexcept
{
    if (!IsReal(widget)) return false;
    auto* function = widget->GetFunctionByNameInChain(L"DecidedPlayer");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 256) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    auto* side_property = FirstParam(function, {L"InSide", L"Side"});
    auto* decide_property = RC::Unreal::CastField<RC::Unreal::FBoolProperty>(
        FirstParam(function, {L"bDecide", L"Decide"}));
    if (!WriteInteger(side_property, params.data(), side)
        || decide_property == nullptr) return false;
    decide_property->SetPropertyValueInContainer(params.data(), decided);
    try { widget->ProcessEvent(function, params.data()); return true; }
    catch (...) { return false; }
}

bool SceneFocusCharacter(RC::Unreal::UObject* scene, std::uint8_t side,
                         std::string_view code) noexcept
{
    auto* function = IsReal(scene)
        ? scene->GetFunctionByNameInChain(L"OnChangeFocusChara") : nullptr;
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 1024) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    auto* side_property = FirstParam(function, {L"InSide", L"Side"});
    auto* code_property = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        FirstParam(function, {L"InCharaCode", L"CharaCode"}));
    auto* color_property = FirstParam(function,
        {L"InCharaColor", L"CharaColor"});
    auto* delay_property = FirstParam(function,
        {L"InDelayCreateTickCount", L"DelayCreateTickCount"});
    if (!WriteInteger(side_property, params.data(), side)
        || !WriteInteger(color_property, params.data(), 0)
        || !WriteInteger(delay_property, params.data(), 0)
        || code_property == nullptr) return false;
    const auto wide = Widen(code);
    code_property->InitializeValue_InContainer(params.data());
    try
    {
        code_property->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(wide));
        scene->ProcessEvent(function, params.data());
    }
    catch (...) { code_property->DestroyValue_InContainer(params.data()); return false; }
    code_property->DestroyValue_InContainer(params.data());
    return true;
}

bool DecideCharacter(RC::Unreal::UObject* scene, std::uint8_t side,
                     std::string_view code) noexcept
{
    auto* function = IsReal(scene)
        ? scene->GetFunctionByNameInChain(L"OnDecidedChara") : nullptr;
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 1024) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    auto* side_property = FirstParam(function, {L"InSide", L"Side"});
    auto* code_property = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        FirstParam(function, {L"InCharaCode", L"CharaCode"}));
    auto* color_property = FirstParam(function,
        {L"InCharaColor", L"CharaColor"});
    auto* weapon_property = RC::Unreal::CastField<RC::Unreal::FStructProperty>(
        FirstParam(function, {L"InDecidedWeapon", L"DecidedWeapon"}));
    auto* source_weapon = RC::Unreal::CastField<RC::Unreal::FStructProperty>(
        scene->GetPropertyByNameInChain(side == 0
            ? L"DecidedWeapon_L" : L"DecidedWeapon_R"));
    auto* motion_raw = FirstParam(function, {L"InMotionTag", L"MotionTag"});
    auto* motion_string = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        motion_raw);
    auto* motion_name = RC::Unreal::CastField<RC::Unreal::FNameProperty>(
        motion_raw);
    if (!WriteInteger(side_property, params.data(), side)
        || !WriteInteger(color_property, params.data(), 0)
        || code_property == nullptr || weapon_property == nullptr
        || source_weapon == nullptr || (!motion_string && !motion_name))
        return false;
    weapon_property->InitializeValue_InContainer(params.data());
    weapon_property->CopyCompleteValue(
        weapon_property->ContainerPtrToValuePtr<void>(params.data()),
        source_weapon->ContainerPtrToValuePtr<void>(scene));
    code_property->InitializeValue_InContainer(params.data());
    if (motion_string) motion_string->InitializeValue_InContainer(params.data());
    const auto wide = Widen(code);
    bool ok{};
    try
    {
        code_property->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(wide));
        if (motion_string)
            motion_string->SetPropertyValueInContainer(
                params.data(), RC::Unreal::FString(L"Decide"));
        else
            *motion_name->ContainerPtrToValuePtr<RC::Unreal::FName>(
                params.data()) = RC::Unreal::FName(L"Decide", RC::Unreal::FNAME_Add);
        scene->ProcessEvent(function, params.data());
        ok = true;
    }
    catch (...) { ok = false; }
    if (motion_string) motion_string->DestroyValue_InContainer(params.data());
    code_property->DestroyValue_InContainer(params.data());
    weapon_property->DestroyValue_InContainer(params.data());
    return ok;
}

bool SelectLocalCharacter(RC::Unreal::UObject* scene,
                          const OnlineAutomationRequest& request) noexcept
{
    auto* session_data = RC::Unreal::UObjectGlobals::StaticFindObject<
        RC::Unreal::UObject*>(nullptr, nullptr,
        STR("/Script/LuxorSessionUtil.Default__LuxorSessionData"));
    bool active{};
    if (!BoolQuery(session_data, L"IsActiveUser", active)) return false;
    const bool right = !active;
    const std::uint8_t side = right ? 1 : 0;
    const auto& code = request.fighter_codes[side];
    auto* widget = ObjectProperty(scene, L"RefCharaSelect");
    std::int32_t cursor{-1};
    if (!CharacterCursor(widget, code, cursor)
        || !SetDecidedPlayer(widget, side, false)
        || !ChangeSelectionFocus(widget, cursor, right)
        || !SceneFocusCharacter(scene, side, code)
        || !SetStringProperty(scene,
            right ? L"DecideCharaCode_R" : L"DecideCharaCode_L", code)
        || !DecideCharacter(scene, side, code)) return false;
    auto* setup_state = CurrentSetupState(scene);
    return IsReal(setup_state) && CallNoParam(setup_state, L"TrySendCharacter");
}

bool WidgetStringCommand(RC::Unreal::UObject* widget,
                         const wchar_t* command,
                         const std::wstring& value) noexcept
{
    if (!IsReal(widget)) return false;
    auto* function = widget->GetFunctionByNameInChain(L"RequestInputCommand");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 4096) return false;
    auto* command_property = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        Param(function, L"CommandName"));
    auto* controller = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        Param(function, L"ControllerId"));
    auto* param = Param(function, L"Param");
    if (!command_property || !controller || !param || param->GetSize() <= 0
        || param->GetSize() > static_cast<int>(sizeof(UIDataObject))) return false;
    UIDataObject data{};
    if (!CreateDataObject(L"Conv_StringToUIDataObject", L"inString",
            value.c_str(), data)
        && !CreateDataObject(L"Conv_StringToUIDataObject", L"InString",
            value.c_str(), data)) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    command_property->InitializeValue_InContainer(params.data());
    bool ok{};
    try
    {
        command_property->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(command));
        *controller->ContainerPtrToValuePtr<std::int32_t>(params.data()) = -1;
        std::memcpy(param->ContainerPtrToValuePtr<void>(params.data()), &data,
            static_cast<std::size_t>(param->GetSize()));
        widget->ProcessEvent(function, params.data());
        ok = true;
    }
    catch (...) { ok = false; }
    Destroy(data);
    command_property->DestroyValue_InContainer(params.data());
    return ok;
}

bool StageCursor(RC::Unreal::UObject* widget, std::string_view code,
                 std::int32_t& cursor) noexcept
{
    cursor = -1;
    auto* function = IsReal(widget) ? widget->GetFunctionByNameInChain(
        L"GetCursorIndexByStageCode") : nullptr;
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 1024) return false;
    auto* code_property = RC::Unreal::CastField<RC::Unreal::FStrProperty>(
        FirstParam(function, {L"InStageCode", L"StageCode"}));
    auto* cursor_property = RC::Unreal::CastField<RC::Unreal::FIntProperty>(
        FirstParam(function, {L"CursorIndex", L"OutCursorIndex", L"ReturnValue"}));
    if (code_property == nullptr || cursor_property == nullptr) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    const auto wide = Widen(code);
    code_property->InitializeValue_InContainer(params.data());
    try
    {
        code_property->SetPropertyValueInContainer(
            params.data(), RC::Unreal::FString(wide));
        widget->ProcessEvent(function, params.data());
        cursor = *cursor_property->ContainerPtrToValuePtr<std::int32_t>(
            params.data());
    }
    catch (...) { cursor = -1; }
    code_property->DestroyValue_InContainer(params.data());
    return cursor >= 0;
}

bool StageWidgetActive(RC::Unreal::UObject* widget) noexcept
{
    if (!IsReal(widget)) return false;
    try
    {
        auto* property = RC::Unreal::CastField<RC::Unreal::FBoolProperty>(
            widget->GetPropertyByNameInChain(L"bActive"));
        return property != nullptr
            && property->GetPropertyValueInContainer(widget);
    }
    catch (...) { return false; }
}

bool RequestPlaySide(RC::Unreal::UObject* manager, std::uint8_t side) noexcept
{
    auto* hub = ObjectProperty(manager, L"SessionHub");
    if (!IsReal(hub))
        hub = RC::Unreal::UObjectGlobals::FindFirstOf(L"LuxorSessionHub");
    if (!IsReal(hub)) return false;
    auto* function = hub->GetFunctionByNameInChain(L"RequestChangePlaySide");
    if (function == nullptr || function->GetPropertiesSize() <= 0
        || function->GetPropertiesSize() > 64) return false;
    std::vector<std::uint8_t> params(
        static_cast<std::size_t>(function->GetPropertiesSize()), 0);
    auto* property = FirstParam(function, {L"side", L"Side", L"InSide"});
    if (!WriteInteger(property, params.data(), side)) return false;
    try { hub->ProcessEvent(function, params.data()); return true; }
    catch (...) { return false; }
}

struct StockReadyChannelState
{
    bool sampled{};
    std::int32_t connect_state{-1};
    std::int32_t channel_state{-1};
    bool can_send{};
};

StockReadyChannelState SampleStockReadyChannel(
    std::uintptr_t image_base) noexcept
{
    StockReadyChannelState result{};
    __try
    {
        using ResolveConnectFn = void* (__fastcall*)();
        using GetChannelFn = void* (__fastcall*)(void*, void*);
        using QueryFn = std::uint8_t (__fastcall*)(void*);
        auto* connect = reinterpret_cast<ResolveConnectFn>(
            image_base + 0x2e56f00)();
        if (connect == nullptr) return result;
        auto** connect_vtable = *reinterpret_cast<void***>(connect);
        if (connect_vtable == nullptr) return result;
        auto* channel = reinterpret_cast<GetChannelFn>(connect_vtable[9])(
            connect, reinterpret_cast<void*>(image_base + 0x40e3c70));
        if (channel == nullptr) return result;
        auto** channel_vtable = *reinterpret_cast<void***>(channel);
        if (channel_vtable == nullptr) return result;
        result.connect_state = static_cast<std::int32_t>(
            reinterpret_cast<QueryFn>(connect_vtable[7])(connect));
        result.channel_state = static_cast<std::int32_t>(
            reinterpret_cast<QueryFn>(channel_vtable[8])(channel));
        result.can_send =
            reinterpret_cast<QueryFn>(channel_vtable[5])(channel) != 0;
        result.sampled = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { result = {}; }
    return result;
}

enum class InjectedInviteConnectionState : std::uint8_t
{
    WaitingManager,
    WaitingActiveConnect,
    WaitingTransportContext,
    WaitingTransport,
    WaitingTransportQuery,
    WaitingRepairBoundary,
    WaitingDeferredConnection,
    Ready,
    Repaired,
    IdentityMismatch,
    TransitionFailed,
    Exception,
};

InjectedInviteConnectionState EnsureInjectedInviteSessionConnection(
    std::uintptr_t image_base) noexcept
{
    __try
    {
        using GetManagerFn = void* (__fastcall*)();
        using GetObjectFn = void* (__fastcall*)(void*);
        using QueryFn = std::uint8_t (__fastcall*)(void*);
        using HandleDeferredFn = void (__fastcall*)(void*, std::uint64_t);

        auto* manager = reinterpret_cast<GetManagerFn>(
            image_base + 0x2e56fd0)();
        if (manager == nullptr)
            return InjectedInviteConnectionState::WaitingManager;
        auto* manager_bytes = static_cast<std::byte*>(manager);
        if (*reinterpret_cast<void**>(manager_bytes + 0x80) != nullptr)
        {
            return InjectedInviteConnectionState::Ready;
        }
        auto* active = *reinterpret_cast<std::byte**>(manager_bytes + 0x60);
        if (active == nullptr)
            return InjectedInviteConnectionState::WaitingActiveConnect;
        if (*reinterpret_cast<void***>(active + 0x18)
            != reinterpret_cast<void**>(image_base + 0x03d27940))
        {
            return InjectedInviteConnectionState::IdentityMismatch;
        }
        const std::uint64_t session_name =
            *reinterpret_cast<std::uint64_t*>(active + 0x30);
        auto** active_vtable = *reinterpret_cast<void***>(active);
        if (session_name == 0 || active_vtable == nullptr
            || active_vtable[31] == nullptr)
        {
            return InjectedInviteConnectionState::WaitingTransportContext;
        }
        auto* transport = static_cast<std::byte*>(
            reinterpret_cast<GetObjectFn>(active_vtable[31])(active));
        if (transport == nullptr)
            return InjectedInviteConnectionState::WaitingTransport;
        if (transport != active + 0xa8)
        {
            return InjectedInviteConnectionState::IdentityMismatch;
        }
        auto** transport_vtable = *reinterpret_cast<void***>(transport);
        if (transport_vtable == nullptr || transport_vtable[17] == nullptr)
        {
            return InjectedInviteConnectionState::WaitingTransportQuery;
        }
        const auto status = *reinterpret_cast<std::uint8_t*>(transport + 0x20);
        const auto ready_state =
            *reinterpret_cast<std::uint8_t*>(transport + 0x21);
        const auto is_host = *reinterpret_cast<std::uint8_t*>(transport + 0x22);
        const auto channel_count =
            *reinterpret_cast<std::uint32_t*>(transport + 0x18);
        const auto connect_state =
            *reinterpret_cast<std::uint8_t*>(active + 0x3d);
        auto ready_query = reinterpret_cast<QueryFn>(transport_vtable[17]);
        // The guest must reach state 4 through the stock opcode-14 exchange.
        // Marking this ActiveConnect transport from the harness skips that
        // exchange and leaves its queued request to time out in state 5.
        if (connect_state != 4)
            return InjectedInviteConnectionState::WaitingRepairBoundary;
        if (status != 1 || is_host != 0 || channel_count == 0
            || channel_count == 0xffffffffu || ready_state != 0xffu
            || ready_query(transport) == 0)
            return InjectedInviteConnectionState::WaitingTransportQuery;

        // Resume the exact stock delegate continuation. It creates the real
        // SessionConnection, initializes its channel registry, and opens the
        // ReadyToConnect channel only after the verified transport-ready gate.
        reinterpret_cast<HandleDeferredFn>(image_base + 0x2e5a9c0)(
            manager, session_name);
        if (*reinterpret_cast<void**>(manager_bytes + 0x80) == nullptr)
            return InjectedInviteConnectionState::WaitingDeferredConnection;
        return InjectedInviteConnectionState::Repaired;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return InjectedInviteConnectionState::Exception;
    }
}

enum class ReadyChannelRetryState : std::uint8_t
{
    WaitingObservation,
    WaitingActiveConnect,
    ObservationMismatch,
    SendFailed,
    Sent,
};

ReadyChannelRetryState ResendObservedReadyChannelState(
    std::uintptr_t image_base, std::uint64_t observation_baseline) noexcept
{
    const auto generation =
        g_ready_observation_generation.load(std::memory_order_acquire);
    if (generation <= observation_baseline)
        return ReadyChannelRetryState::WaitingObservation;
    void* observed_active =
        g_observed_ready_active.load(std::memory_order_relaxed);
    const auto requested_state =
        g_observed_ready_state.load(std::memory_order_relaxed);
    __try
    {
        using GetManagerFn = void* (__fastcall*)();
        using SendFn = bool (__fastcall*)(void*, std::uint8_t);
        auto* manager = reinterpret_cast<GetManagerFn>(
            image_base + 0x2e56fd0)();
        auto* active = manager != nullptr
            ? *reinterpret_cast<std::byte**>(
                static_cast<std::byte*>(manager) + 0x60)
            : nullptr;
        if (active == nullptr)
            return ReadyChannelRetryState::WaitingActiveConnect;
        if (active != observed_active
            || *reinterpret_cast<std::uint8_t*>(active + 0x3d) != 3)
            return ReadyChannelRetryState::ObservationMismatch;
        return reinterpret_cast<SendFn>(image_base + 0x2e6d4d0)(
                   active, requested_state)
            ? ReadyChannelRetryState::Sent
            : ReadyChannelRetryState::SendFailed;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return ReadyChannelRetryState::SendFailed;
    }
}

std::int32_t SampleActiveConnectState(std::uintptr_t image_base) noexcept
{
    __try
    {
        using GetManagerFn = void* (__fastcall*)();
        auto* manager = reinterpret_cast<GetManagerFn>(
            image_base + 0x2e56fd0)();
        auto* active = manager != nullptr
            ? *reinterpret_cast<std::byte**>(
                static_cast<std::byte*>(manager) + 0x60)
            : nullptr;
        return active != nullptr
            ? static_cast<std::int32_t>(
                *reinterpret_cast<std::uint8_t*>(active + 0x3d))
            : -1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

enum class HostSessionTransportState : std::uint8_t
{
    Waiting,
    Ready,
    Failed,
};

HostSessionTransportState EnsureHostSessionTransportReady(
    std::uintptr_t image_base) noexcept
{
    __try
    {
        using GetManagerFn = void* (__fastcall*)();
        using MarkTransportReadyFn = void (__fastcall*)(void*);
        auto* manager = reinterpret_cast<GetManagerFn>(
            image_base + 0x2e56fd0)();
        auto* session_connection = manager != nullptr
            ? *reinterpret_cast<std::byte**>(
                static_cast<std::byte*>(manager) + 0x80)
            : nullptr;
        if (session_connection == nullptr)
            return HostSessionTransportState::Waiting;
        // The peer-ready handler checks the transport embedded in the real
        // SessionConnection, not the unrelated ActiveConnect transport.
        auto* transport = session_connection + 0x60;
        auto& ready_state = *reinterpret_cast<std::uint8_t*>(transport + 0x21);
        if (ready_state != 0xffu)
            reinterpret_cast<MarkTransportReadyFn>(
                image_base + 0x2e5a420)(transport);
        return ready_state == 0xffu
            ? HostSessionTransportState::Ready
            : HostSessionTransportState::Failed;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return HostSessionTransportState::Failed;
    }
}

bool RequestStockReady(std::uintptr_t image_base,
                       RC::Unreal::UObject* manager,
                       const StockReadyChannelState& channel) noexcept
{
    if (!channel.sampled || channel.connect_state != 3
        || channel.channel_state != 1 || !channel.can_send)
        return false;
    auto* session_hub = ObjectProperty(manager, L"SessionHub");
    if (!IsReal(session_hub)) return false;
    __try
    {
        // This is the sender reached by the stock InRoomMenu Ready wrapper.
        // Calling it here preserves its success result, so the harness never
        // mistakes the wrapper's void return for a transmitted request.
        using RequestReadyFn = bool (__fastcall*)(void*, std::int32_t);
        return reinterpret_cast<RequestReadyFn>(
            image_base + 0x2e1d470)(session_hub, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

OnlineRoomState ObserveExactMatchContent(
    const OnlineAutomationRequest& request, RC::Unreal::UObject* scene,
    RC::Unreal::UObject* manager, bool mismatch_is_terminal,
    std::string& detail) noexcept
{
    (void)scene;
    (void)manager;
    const auto snapshot = ReadBattleSyncObservation();
    if (!snapshot.present)
    {
        detail = "exact_match_content_pending:no_battle_sync";
        return OnlineRoomState::Waiting;
    }
    if (!snapshot.detailed_valid)
    {
        if (!snapshot.flags_readable)
        {
            detail = "exact_match_content_pending:sync_unreadable";
            return OnlineRoomState::Waiting;
        }
        if (snapshot.characters_received == 0 || snapshot.stage_received == 0)
        {
            detail = "exact_match_content_pending:sync_not_received:flags="
                + std::to_string(snapshot.profile_received) + "/"
                + std::to_string(snapshot.characters_received) + "/"
                + std::to_string(snapshot.stage_received) + ":generation="
                + std::to_string(snapshot.generation);
            return OnlineRoomState::Waiting;
        }
        if (!snapshot.raw_readable)
        {
            detail = "exact_match_content_invalid:raw_unreadable";
            return OnlineRoomState::Failed;
        }
        const auto& raw = snapshot.raw;
        detail = "exact_match_content_invalid:raw="
            + std::string(raw.fighter_text[0]) + "["
            + std::to_string(raw.fighter_count[0]) + "/"
            + std::to_string(raw.fighter_capacity[0]) + "]/"
            + std::string(raw.fighter_text[1]) + "["
            + std::to_string(raw.fighter_count[1]) + "/"
            + std::to_string(raw.fighter_capacity[1]) + "]:stage="
            + std::to_string(raw.stage) + ":random="
            + std::to_string(raw.random);
        return mismatch_is_terminal
            ? OnlineRoomState::Failed : OnlineRoomState::Waiting;
    }
    const auto& content = snapshot.detailed.content;
    if (std::string_view{content.fighter_codes[0].data()}
            != request.fighter_codes[0]
        || std::string_view{content.fighter_codes[1].data()}
            != request.fighter_codes[1]
        || std::string_view{content.stage_code.data()} != request.stage_code
        || content.stage_was_random)
    {
        detail = "exact_match_content_mismatch:observed="
            + std::string(content.fighter_codes[0].data()) + "/"
            + std::string(content.fighter_codes[1].data()) + ":stage="
            + std::string(content.stage_code.data()) + ":random="
            + (content.stage_was_random ? "1" : "0");
        return mismatch_is_terminal
            ? OnlineRoomState::Failed : OnlineRoomState::Waiting;
    }
    detail = "exact_match_content_verified:"
        + request.fighter_codes[0] + "/" + request.fighter_codes[1]
        + ":stage=" + request.stage_code + ":map="
        + request.display_map_name;
    return OnlineRoomState::Complete;
}
}

bool OnlineRoomAutomation::Bind(std::uintptr_t image_base) noexcept
{
    constexpr std::array<std::byte, 8> kDestroy{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}};
    g_destroy_path = {};
    if (image_base == 0
        || !SignatureMatches(image_base + 0x2ed6a80, kDestroy)
        || !InstallReadyChannelObserver(image_base)
        || !InstallBattleSyncReceiveObserver(image_base)) return false;
    image_base_ = image_base;
    g_destroy_path = reinterpret_cast<DestroyPathFn>(image_base + 0x2ed6a80);
    return true;
}

void OnlineRoomAutomation::Reset(const OnlineAutomationRequest& request) noexcept
{
    ResetBattleSyncObservation();
    request_ = request;
    step_ = Step::NavigateToPlayerMatch;
    last_scene_.clear();
    scene_ticks_ = 0;
    step_ticks_ = 0;
    create_retries_ = 0;
    main_menu_route_step_ = 0;
    title_top_requested_ = false;
    title_decide_stage_ = 0;
    lobby_id_ = 0;
    observed_local_steam_id_ = 0;
    setup_scene_ticks_ = 0;
    stage_focus_tick_ = 0;
    lobby_metadata_requested_ = false;
    native_invite_queued_ = false;
    play_side_requested_ = false;
    session_connection_ready_ = false;
    peer_connect_ready_published_ = false;
    host_session_transport_ready_ = false;
    host_transport_ready_published_ = false;
    ready_channel_retry_sent_ = false;
    guest_session_ready_published_ = false;
    ready_observation_baseline_ =
        g_ready_observation_generation.load(std::memory_order_acquire);
    ready_requested_ = false;
    character_requested_ = false;
    stage_focus_requested_ = false;
    stage_decide_requested_ = false;
    match_content_verified_ = false;
    battle_end_and_lobby_transition_requested_ = false;
}

OnlineRoomState OnlineRoomAutomation::Tick(std::string& detail) noexcept
{
    auto* manager = FindManager();
    auto* scene = CurrentScene(manager);
    if (!IsReal(scene)) { detail = "game_flow_context_unavailable"; return OnlineRoomState::Waiting; }
    const std::string scene_name = ClassName(scene);
    if (scene_name != last_scene_)
    {
        last_scene_ = scene_name;
        scene_ticks_ = 0;
        title_top_requested_ = false;
        title_decide_stage_ = 0;
    }
    ++scene_ticks_;
    ++step_ticks_;

    if (scene_name.find("InitScene") != std::string::npos
        || scene_name.find("AdvertiseScene") != std::string::npos)
    { detail = scene_name; return OnlineRoomState::Waiting; }

    if (scene_name.find("TitleScene") != std::string::npos)
    {
        auto* behavior = ObjectProperty(scene, L"MainBehavior");
        if (!IsReal(ObjectProperty(scene, L"RefTitleMenu")) || !IsReal(behavior))
        { detail = "title_startup_pending"; return OnlineRoomState::Waiting; }
        if (!title_top_requested_)
        {
            if (!CallStringParam(behavior, L"ChangeState", L"Top"))
            { detail = "title_top_failed"; return OnlineRoomState::Failed; }
            title_top_requested_ = true;
        }
        auto* machine = ObjectProperty(behavior, L"Machine");
        const std::string state_code = StringProperty(machine, L"CurrentStateCode");
        auto* current_state = ObjectProperty(machine, L"CurrentState");
        if (state_code == "Top" && scene_ticks_ >= 8 && title_decide_stage_ == 0
            && CallNoParam(current_state, L"RequestDecideMainUser"))
        { title_decide_stage_ = 1; scene_ticks_ = 0; }
        else if (state_code == "Top" && scene_ticks_ >= 2 && title_decide_stage_ == 1)
        {
            auto* signin = ObjectProperty(manager, L"SigninManager");
            if (IsReal(signin) && CallIntParam(signin, L"RequestDecideMainUser", 0))
            { title_decide_stage_ = 2; scene_ticks_ = 0; }
        }
        else if (state_code == "Top" && scene_ticks_ >= 8 && title_decide_stage_ == 2
                 && CallNoParam(current_state, L"OnDecidedMainUser"))
        { title_decide_stage_ = 3; scene_ticks_ = 0; }
        else if (state_code == "Top" && scene_ticks_ >= 8 && title_decide_stage_ == 3
                 && CallNoParam(current_state, L"OnDecidedTitle"))
        { title_decide_stage_ = 4; scene_ticks_ = 0; }
        else if (state_code == "Top" && scene_ticks_ >= 8 && title_decide_stage_ == 4
                 && CallNoParam(current_state, L"FinishFadeout"))
        { title_decide_stage_ = 5; scene_ticks_ = 0; }
        detail = "title_state:" + state_code;
        return OnlineRoomState::Waiting;
    }

    if (request_.action == OnlineAutomationAction::MatchTeardown)
    {
        if (request_.battle_result < 0 || request_.battle_result > 2)
        {
            detail = "invalid_battle_end_result";
            return OnlineRoomState::Failed;
        }
        const auto scene_kind =
            scene_name.find("PlayerMatchLobbyScene") != std::string::npos
                ? MatchTeardownScene::PlayerMatchLobby
                : scene_name.find("SetupScene") != std::string::npos
                    ? MatchTeardownScene::PlayerMatchSetup
                    : scene_name.find("PlayerMatchScene") != std::string::npos
                        ? MatchTeardownScene::ActivePlayerMatch
                        : MatchTeardownScene::Other;
        const auto plan = PlanMatchTeardown(
            scene_kind, battle_end_and_lobby_transition_requested_);
        if (plan.state == OnlineRoomState::Complete)
        {
            detail = "returned_to_player_match_lobby";
            return plan.state;
        }
        if (plan.request_battle_end)
        {
            if (!CallNoParam(scene, L"RequestBattleEnd"))
            {
                detail = "player_match_request_battle_end_failed";
                return OnlineRoomState::Failed;
            }
            if (!plan.request_lobby_transition
                || !RequestPlayerMatchLobbyTransition(scene))
            {
                detail = "player_match_lobby_transition_failed";
                return OnlineRoomState::Failed;
            }
            battle_end_and_lobby_transition_requested_ = true;
            detail = "battle_end_and_lobby_transition_requested";
            return plan.state;
        }
        if (scene_kind != MatchTeardownScene::ActivePlayerMatch)
        {
            detail = "waiting_for_active_player_match";
            return plan.state;
        }
        detail = "waiting_for_player_match_lobby";
        return plan.state;
    }

    if (request_.action == OnlineAutomationAction::MatchSetup)
    {
        static SteamBindings steam;
        if (!steam.Initialize())
        {
            detail = "steam_bindings_unavailable";
            return OnlineRoomState::Waiting;
        }
        observed_local_steam_id_ = steam.local_id;
        if (request_.lobby_id == 0 || request_.local_steam_id == 0
            || request_.peer_steam_id == 0
            || request_.fighter_codes[0].empty()
            || request_.fighter_codes[1].empty()
            || request_.stage_code.empty()
            || request_.authored_stage_code.empty()
            || request_.ui_stage_code.empty()
            || request_.display_map_name.empty())
        {
            detail = "invalid_match_setup_request";
            return OnlineRoomState::Failed;
        }
        if (steam.local_id != request_.local_steam_id)
        {
            detail = "local_steam_identity_mismatch";
            return OnlineRoomState::Failed;
        }

        if (scene_name.find("PlayerMatchScene") != std::string::npos
            && scene_name.find("LobbyScene") == std::string::npos
            && scene_name.find("SetupScene") == std::string::npos)
        {
            lobby_id_ = request_.lobby_id;
            if (!match_content_verified_)
                return ObserveExactMatchContent(
                    request_, scene, manager, true, detail);
            detail = "exact_match_content_verified:"
                + request_.fighter_codes[0] + "/" + request_.fighter_codes[1]
                + ":stage=" + request_.stage_code + ":map="
                + request_.display_map_name;
            return OnlineRoomState::Complete;
        }

        if (scene_name.find("PlayerMatchSetupScene") != std::string::npos)
        {
            ++setup_scene_ticks_;
            if (!match_content_verified_)
            {
                // The stock receiver publishes complete intermediate syncs
                // while character/stage selection is still in progress (the
                // initial stage may still be Random).  Observe those updates,
                // but make identity disagreement terminal only after setup
                // has handed off to PlayerMatchScene.
                std::string content_detail;
                const auto content = ObserveExactMatchContent(
                    request_, scene, manager, false, content_detail);
                if (content == OnlineRoomState::Failed)
                {
                    detail = std::move(content_detail);
                    return OnlineRoomState::Failed;
                }
                match_content_verified_ =
                    content == OnlineRoomState::Complete;
            }
            auto* phase = ObjectProperty(scene, L"CurrentPhase");
            const bool character_phase = IdentityContains(phase, "CharaSelectExec");
            if (character_phase && !character_requested_)
            {
                auto* session_data = RC::Unreal::UObjectGlobals::StaticFindObject<
                    RC::Unreal::UObject*>(nullptr, nullptr,
                    STR("/Script/LuxorSessionUtil.Default__LuxorSessionData"));
                bool active{};
                if (!BoolQuery(session_data, L"IsActiveUser", active))
                {
                    detail = "waiting_for_active_user_authority";
                    return OnlineRoomState::Waiting;
                }
                const std::uint32_t delay = active ? 62u : 2u;
                if (setup_scene_ticks_ < delay)
                {
                    detail = "waiting_for_character_order";
                    return OnlineRoomState::Waiting;
                }
                if (SelectLocalCharacter(scene, request_))
                {
                    character_requested_ = true;
                    detail = active
                        ? "left_character_sent" : "right_character_sent";
                    return OnlineRoomState::Waiting;
                }
                if (setup_scene_ticks_ > 600)
                {
                    detail = "character_selection_timeout";
                    return OnlineRoomState::Failed;
                }
                detail = "character_selection_pending";
                return OnlineRoomState::Waiting;
            }

            auto* stage = ObjectProperty(scene, L"RefStageSelect");
            if (StageWidgetActive(stage) && !stage_focus_requested_)
            {
                if (setup_scene_ticks_ > 900)
                {
                    detail = "stage_focus_timeout:" + request_.display_map_name;
                    return OnlineRoomState::Failed;
                }
                std::int32_t selected{-1};
                const auto wide = Widen(request_.ui_stage_code);
                if (!StageCursor(stage, request_.ui_stage_code, selected))
                {
                    detail = "stage_cursor_pending:ui="
                        + request_.ui_stage_code;
                    return OnlineRoomState::Waiting;
                }
                if (!ChangeSelectionFocus(stage, selected, false))
                {
                    detail = "stage_focus_mutation_pending:cursor="
                        + std::to_string(selected);
                    return OnlineRoomState::Waiting;
                }
                if (!WidgetStringCommand(stage, L"ChangeStageFocus", wide))
                {
                    detail = "stage_focus_command_pending:cursor="
                        + std::to_string(selected);
                    return OnlineRoomState::Waiting;
                }
                stage_focus_requested_ = true;
                stage_focus_tick_ = setup_scene_ticks_;
                detail = "stage_focused:" + request_.display_map_name;
                return OnlineRoomState::Waiting;
            }
            if (StageWidgetActive(stage) && stage_focus_requested_
                && !stage_decide_requested_
                && setup_scene_ticks_ >= stage_focus_tick_ + 30)
            {
                if (!WidgetStringCommand(
                        stage, L"DecideStage",
                        Widen(request_.ui_stage_code)))
                {
                    detail = "stage_decide_failed:" + request_.display_map_name;
                    return OnlineRoomState::Failed;
                }
                stage_decide_requested_ = true;
                detail = "stage_decided:" + request_.display_map_name;
                return OnlineRoomState::Waiting;
            }
            if (setup_scene_ticks_ > 1800)
            {
                detail = "setup_scene_timeout:" + request_.display_map_name;
                return OnlineRoomState::Failed;
            }
            detail = "waiting_in_setup:" + ClassName(phase) + ":"
                + request_.display_map_name;
            return OnlineRoomState::Waiting;
        }

        if (scene_name.find("PlayerMatchLobbyScene") != std::string::npos)
        {
            auto* lobby_state = CurrentLobbyState(scene);
            if (!IdentityContains(lobby_state, "PlayerMatchInRoomState"))
            {
                if (step_ticks_ > 1800)
                {
                    detail = "in_room_timeout:" + ClassName(lobby_state);
                    return OnlineRoomState::Failed;
                }
                detail = "waiting_for_in_room:" + ClassName(lobby_state);
                return OnlineRoomState::Waiting;
            }
            std::uint64_t observed_lobby{};
            if (!ObserveAnyLobby(image_base_, observed_lobby)
                || observed_lobby != request_.lobby_id)
            {
                detail = "waiting_for_exact_named_session";
                return OnlineRoomState::Waiting;
            }
            lobby_id_ = observed_lobby;
            const std::uint64_t observed_owner = steam.owner(
                steam.matchmaking, observed_lobby);
            const std::uint64_t expected_owner =
                request_.role == OnlineAutomationRole::Host
                    ? request_.local_steam_id : request_.peer_steam_id;
            if (observed_owner != expected_owner)
            {
                if (observed_owner == 0
                    && (ready_requested_ || session_connection_ready_))
                {
                    detail = "waiting_for_setup_lobby_teardown";
                    return OnlineRoomState::Waiting;
                }
                if (request_.role == OnlineAutomationRole::Sandbox
                    && session_connection_ready_
                    && observed_owner == request_.local_steam_id)
                {
                    detail = "waiting_for_setup_owner_transfer";
                    return OnlineRoomState::Waiting;
                }
                detail = "lobby_owner_identity_mismatch:expected="
                    + std::to_string(expected_owner) + ":observed="
                    + std::to_string(observed_owner);
                return OnlineRoomState::Failed;
            }
            if (!play_side_requested_)
            {
                if (!RequestPlaySide(manager,
                        request_.role == OnlineAutomationRole::Host ? 0 : 1))
                {
                    detail = "play_side_request_pending";
                    return OnlineRoomState::Waiting;
                }
                play_side_requested_ = true;
                detail = "play_side_requested";
                return OnlineRoomState::Waiting;
            }
            const int members = steam.member_count(
                steam.matchmaking, observed_lobby);
            if (members != 2)
            {
                detail = "waiting_for_authenticated_pair:"
                    + std::to_string(members);
                return OnlineRoomState::Waiting;
            }
            if (request_.role == OnlineAutomationRole::Sandbox)
            {
                const std::int32_t connect_state =
                    SampleActiveConnectState(image_base_);
                if (!peer_connect_ready_published_
                    && (connect_state == 3 || connect_state == 4))
                {
                    steam.set_member_data(steam.matchmaking, observed_lobby,
                        "HORSE_Q_PEER_CONNECT_READY", "1");
                    peer_connect_ready_published_ = true;
                }
                if (connect_state == 3 && !ready_channel_retry_sent_)
                {
                    const char* host_ready = steam.member_data(
                        steam.matchmaking, observed_lobby,
                        request_.peer_steam_id,
                        "HORSE_Q_HOST_TRANSPORT_READY");
                    if (host_ready == nullptr
                        || std::strcmp(host_ready, "1") != 0)
                    {
                        detail = "waiting_for_host_transport_ready";
                        return OnlineRoomState::Waiting;
                    }
                    const auto retry = ResendObservedReadyChannelState(
                        image_base_, ready_observation_baseline_);
                    if (retry == ReadyChannelRetryState::WaitingObservation
                        || retry == ReadyChannelRetryState::WaitingActiveConnect)
                    {
                        detail = "ready_channel_retry_pending:"
                            + std::to_string(static_cast<unsigned>(retry));
                        return OnlineRoomState::Waiting;
                    }
                    if (retry != ReadyChannelRetryState::Sent)
                    {
                        detail = "ready_channel_retry_failed:"
                            + std::to_string(static_cast<unsigned>(retry));
                        return OnlineRoomState::Failed;
                    }
                    ready_channel_retry_sent_ = true;
                    detail = "ready_channel_retry_sent";
                    return OnlineRoomState::Waiting;
                }
                const auto connection = EnsureInjectedInviteSessionConnection(
                    image_base_);
                if (connection == InjectedInviteConnectionState::IdentityMismatch
                    || connection == InjectedInviteConnectionState::TransitionFailed
                    || connection == InjectedInviteConnectionState::Exception)
                {
                    detail = "invite_session_connection_failed:"
                        + std::to_string(static_cast<unsigned>(connection));
                    return OnlineRoomState::Failed;
                }
                if (connection != InjectedInviteConnectionState::Ready
                    && connection != InjectedInviteConnectionState::Repaired)
                {
                    detail = "invite_session_connection_pending:"
                        + std::to_string(static_cast<unsigned>(connection));
                    return OnlineRoomState::Waiting;
                }
                session_connection_ready_ = true;
                if (!guest_session_ready_published_)
                {
                    steam.set_member_data(steam.matchmaking, observed_lobby,
                        "HORSE_Q_GUEST_SESSION_READY", "1");
                    const char* published = steam.member_data(
                        steam.matchmaking, observed_lobby,
                        request_.local_steam_id,
                        "HORSE_Q_GUEST_SESSION_READY");
                    if (published == nullptr
                        || std::strcmp(published, "1") != 0)
                    {
                        detail = "publishing_guest_session_ready";
                        return OnlineRoomState::Waiting;
                    }
                    guest_session_ready_published_ = true;
                }
            }
            else
            {
                if (!host_session_transport_ready_)
                {
                    const char* peer_ready = steam.member_data(
                        steam.matchmaking, observed_lobby,
                        request_.peer_steam_id,
                        "HORSE_Q_PEER_CONNECT_READY");
                    if (peer_ready == nullptr
                        || std::strcmp(peer_ready, "1") != 0)
                    {
                        detail = "waiting_for_peer_connect_state3";
                        return OnlineRoomState::Waiting;
                    }
                    const auto transport = EnsureHostSessionTransportReady(
                        image_base_);
                    if (transport == HostSessionTransportState::Failed)
                    {
                        detail = "host_session_transport_ready_failed";
                        return OnlineRoomState::Failed;
                    }
                    if (transport == HostSessionTransportState::Waiting)
                    {
                        detail = "host_session_transport_pending";
                        return OnlineRoomState::Waiting;
                    }
                    host_session_transport_ready_ = true;
                }
                if (!host_transport_ready_published_)
                {
                    steam.set_member_data(steam.matchmaking, observed_lobby,
                        "HORSE_Q_HOST_TRANSPORT_READY", "1");
                    const char* published = steam.member_data(
                        steam.matchmaking, observed_lobby,
                        request_.local_steam_id,
                        "HORSE_Q_HOST_TRANSPORT_READY");
                    if (published == nullptr
                        || std::strcmp(published, "1") != 0)
                    {
                        detail = "publishing_host_transport_ready";
                        return OnlineRoomState::Waiting;
                    }
                    host_transport_ready_published_ = true;
                }
                if (!guest_session_ready_published_)
                {
                    const char* guest_ready = steam.member_data(
                        steam.matchmaking, observed_lobby,
                        request_.peer_steam_id,
                        "HORSE_Q_GUEST_SESSION_READY");
                    if (guest_ready == nullptr
                        || std::strcmp(guest_ready, "1") != 0)
                    {
                        detail = "waiting_for_guest_session_ready";
                        return OnlineRoomState::Waiting;
                    }
                    guest_session_ready_published_ = true;
                }
            }
            if (!ready_requested_ && scene_ticks_ >= 8)
            {
                const StockReadyChannelState ready =
                    SampleStockReadyChannel(image_base_);
                if (!ready.sampled || ready.connect_state != 3
                    || ready.channel_state != 1 || !ready.can_send)
                {
                    detail = "stock_ready_gate:connect="
                        + std::to_string(ready.connect_state)
                        + ":channel=" + std::to_string(ready.channel_state)
                        + ":can_send=" + (ready.can_send ? "1" : "0");
                    return OnlineRoomState::Waiting;
                }
                if (!RequestStockReady(image_base_, manager, ready))
                {
                    detail = "stock_ready_send_retry";
                    return OnlineRoomState::Waiting;
                }
                ready_requested_ = true;
                detail = "stock_ready_requested";
                return OnlineRoomState::Waiting;
            }
            detail = ready_requested_
                ? "waiting_for_stock_setup" : "stock_ready_settle";
            return OnlineRoomState::Waiting;
        }

        if (request_.role == OnlineAutomationRole::Sandbox
            && scene_name.find("MainMenuScene") != std::string::npos)
        {
            if (steam.owner(steam.matchmaking, request_.lobby_id)
                    != request_.peer_steam_id)
            {
                if (!lobby_metadata_requested_)
                {
                    if (!steam.request_data(steam.matchmaking, request_.lobby_id))
                    {
                        detail = "lobby_metadata_request_failed";
                        return OnlineRoomState::Failed;
                    }
                    lobby_metadata_requested_ = true;
                }
                detail = "waiting_for_authenticated_lobby_owner";
                return OnlineRoomState::Waiting;
            }
            if (!steam.MetadataReady(request_.lobby_id))
            {
                if (!lobby_metadata_requested_)
                {
                    if (!steam.request_data(steam.matchmaking, request_.lobby_id))
                    {
                        detail = "lobby_metadata_request_failed";
                        return OnlineRoomState::Failed;
                    }
                    lobby_metadata_requested_ = true;
                }
                detail = "waiting_for_lobby_metadata";
                return OnlineRoomState::Waiting;
            }
            if (!native_invite_queued_)
            {
                if (!QueueStockInvite(image_base_, request_.lobby_id,
                        request_.peer_steam_id))
                {
                    detail = "stock_invite_queue_failed";
                    return OnlineRoomState::Failed;
                }
                native_invite_queued_ = true;
                detail = "stock_invite_queued";
                return OnlineRoomState::Waiting;
            }
            detail = "waiting_for_stock_invite_transition";
            return OnlineRoomState::Waiting;
        }

        detail = "waiting_for_match_setup_scene:" + scene_name;
        return OnlineRoomState::Waiting;
    }

    if (scene_name.find("PlayerMatchLobbyScene") == std::string::npos)
    {
        if (scene_name.find("MainMenuScene") == std::string::npos)
        { detail = "waiting_for_main_menu:" + scene_name; return OnlineRoomState::Waiting; }
        if (step_ != Step::NavigateToPlayerMatch)
        { detail = "left_player_match:" + scene_name; return OnlineRoomState::Failed; }
        if (step_ticks_ >= 8)
        {
            bool accepted{};
            std::string action_detail;
            switch (main_menu_route_step_)
            {
            case 0: accepted = SemanticMainMenuAction(scene, L"ENETWORK", false, action_detail); break;
            case 1: accepted = SemanticMainMenuAction(scene, L"ENETWORK", true, action_detail); break;
            case 2: accepted = SemanticMainMenuAction(scene, L"EPLAYERMATCH", false, action_detail); break;
            case 3: accepted = SemanticMainMenuAction(scene, L"EPLAYERMATCH", true, action_detail); break;
            default: accepted = EnterPlayerMatch(scene); break;
            }
            if (!accepted)
            { detail = "semantic_main_menu_action_pending:" +
                    std::to_string(main_menu_route_step_) + ":" + action_detail;
              return OnlineRoomState::Waiting; }
            if (main_menu_route_step_ < 4) ++main_menu_route_step_;
            step_ticks_ = 0;
        }
        detail = "semantic_main_menu_route:" +
            std::to_string(main_menu_route_step_);
        return OnlineRoomState::Waiting;
    }

    auto* lobby_state = CurrentLobbyState(scene);
    if (IdentityContains(lobby_state, "PlayerMatchInRoomState"))
    {
        static SteamBindings steam;
        std::uint64_t observed_lobby{};
        if (!ObserveAnyLobby(image_base_, observed_lobby)
            || !steam.Initialize())
        {
            detail = "host_room_waiting_for_named_session";
            return OnlineRoomState::Waiting;
        }
        lobby_id_ = observed_lobby;
        observed_local_steam_id_ = steam.local_id;
        if (steam.owner(steam.matchmaking, lobby_id_) != steam.local_id)
        {
            detail = "host_room_owner_mismatch";
            return OnlineRoomState::Failed;
        }
        // k_ELobbyTypePrivate == 0. The sandbox joins only through the exact
        // authenticated invite event, so the room is never left browsable.
        if (!steam.set_type(steam.matchmaking, lobby_id_, 0))
        {
            detail = "host_room_private_transition_failed";
            return OnlineRoomState::Failed;
        }
        detail = "host_room_created_in_room";
        return OnlineRoomState::Complete;
    }
    if (step_ == Step::NavigateToPlayerMatch)
    { step_ = Step::RequestMakeRoom; step_ticks_ = 0; }
    if (step_ == Step::RequestMakeRoom)
    {
        if (!RequestLobbyState(scene, L"MakeRoom"))
        { detail = "request_make_room_failed"; return OnlineRoomState::Failed; }
        step_ = Step::PollMakeRoom; step_ticks_ = 0;
        detail = "make_room_requested";
        return OnlineRoomState::Waiting;
    }
    if (step_ == Step::PollMakeRoom)
    {
        if (!CallNoParam(scene, L"PollingChangeState"))
        { detail = "poll_make_room_failed"; return OnlineRoomState::Failed; }
        step_ = Step::SendCreateCommand; step_ticks_ = 0;
        detail = "make_room_polled";
        return OnlineRoomState::Waiting;
    }
    if (step_ == Step::SendCreateCommand)
    {
        if (!IdentityContains(lobby_state, "PlayerMatchMakeRoomState"))
        {
            if (step_ticks_ > 240) { detail = "make_room_state_timeout"; return OnlineRoomState::Failed; }
            detail = "waiting_for_make_room_state:" + ClassName(lobby_state);
            return OnlineRoomState::Waiting;
        }
        if (!SendCreateCommand(scene, lobby_state))
        { detail = "start_create_room_command_failed"; return OnlineRoomState::Failed; }
        step_ = Step::PollCreateCommand; step_ticks_ = 0;
        detail = "start_create_room_dispatched";
        return OnlineRoomState::Waiting;
    }
    if (step_ == Step::PollCreateCommand)
    {
        if (!CallNoParam(scene, L"PollingChangeState"))
        { detail = "poll_create_room_failed"; return OnlineRoomState::Failed; }
        step_ = Step::WaitForInRoom; step_ticks_ = 0;
        detail = "create_room_polled";
        return OnlineRoomState::Waiting;
    }
    if (step_ == Step::WaitForInRoom)
    {
        if (step_ticks_ > 120 && create_retries_ < 3
            && IdentityContains(lobby_state, "PlayerMatchMakeRoomState"))
        {
            ++create_retries_;
            if (!SendCreateCommand(scene, lobby_state))
            { detail = "start_create_room_retry_failed"; return OnlineRoomState::Failed; }
            (void)CallNoParam(scene, L"PollingChangeState");
            step_ticks_ = 0;
            detail = "start_create_room_retried";
            return OnlineRoomState::Waiting;
        }
        if (step_ticks_ > 600)
        { detail = "create_room_in_room_timeout"; return OnlineRoomState::Failed; }
        detail = "waiting_for_in_room:" + ClassName(lobby_state);
        return OnlineRoomState::Waiting;
    }
    detail = "invalid_room_automation_state";
    return OnlineRoomState::Failed;
}
}
