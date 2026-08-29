#include "OnlineRoomAutomation.hpp"

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
#include <cwchar>
#include <vector>

namespace Horse::Qualification
{
namespace
{
using DestroyPathFn = void (__fastcall*)(void*);
DestroyPathFn g_destroy_path{};

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
}

bool OnlineRoomAutomation::Bind(std::uintptr_t image_base) noexcept
{
    constexpr std::array<std::byte, 8> kDestroy{
        std::byte{0x48}, std::byte{0x89}, std::byte{0x5c}, std::byte{0x24},
        std::byte{0x08}, std::byte{0x57}, std::byte{0x48}, std::byte{0x83}};
    g_destroy_path = {};
    if (image_base == 0
        || !SignatureMatches(image_base + 0x2ed6a80, kDestroy)) return false;
    g_destroy_path = reinterpret_cast<DestroyPathFn>(image_base + 0x2ed6a80);
    return true;
}

void OnlineRoomAutomation::Reset() noexcept
{
    step_ = Step::NavigateToPlayerMatch;
    last_scene_.clear();
    scene_ticks_ = 0;
    step_ticks_ = 0;
    create_retries_ = 0;
    main_menu_route_step_ = 0;
    title_top_requested_ = false;
    title_decide_stage_ = 0;
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
    { detail = "host_room_created_in_room"; return OnlineRoomState::Complete; }
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
