// ============================================================================
// Horse::FightRequestDelayFramesHook
//
// Replaces the fight-request Preferred Side row with a native LuxLabeledSpinBox
// Frame Delay row. This is intentionally UI-only: it binds to a private object
// store key (`delayFrames`) and does not feed any battle or network code.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace Horse
{
    class FightRequestDelayFramesHook
    {
    public:
        static FightRequestDelayFramesHook& instance()
        {
            static FightRequestDelayFramesHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire)) return true;

            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[FightRequestDelayFramesHook] NativeBinding image base "
                        "missing; cannot install\n"));
                return false;
            }

            resolve_helpers(base);
            if (!helpers_ready())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[FightRequestDelayFramesHook] helper RVAs unresolved; "
                        "delay row disabled\n"));
                return false;
            }

            const uintptr_t build_target = base + kBuildSpinBoxRVA;
            m_build_spinbox_trampoline = 0;
            m_build_spinbox_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(build_target),
                reinterpret_cast<uint64_t>(&FightRequestDelayFramesHook::build_spinbox_detour),
                &m_build_spinbox_trampoline);

            if (!m_build_spinbox_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[FightRequestDelayFramesHook] x64Detour::hook() "
                        "failed on BuildSpinBox (target=0x{:X})\n"),
                    build_target);
                m_build_spinbox_detour.reset();
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[FightRequestDelayFramesHook] installed safe replacement "
                    "hook (BuildSpinBox=0x{:X}/0x{:X})\n"),
                build_target, static_cast<uintptr_t>(m_build_spinbox_trampoline));
            return true;
        }

        void uninstall()
        {
            if (!m_installed.exchange(false)) return;
            if (m_build_spinbox_detour)
            {
                m_build_spinbox_detour->unHook();
                m_build_spinbox_detour.reset();
            }
            m_build_spinbox_trampoline = 0;
        }

    private:
        FightRequestDelayFramesHook() = default;
        ~FightRequestDelayFramesHook() { uninstall(); }
        FightRequestDelayFramesHook(const FightRequestDelayFramesHook&) = delete;
        FightRequestDelayFramesHook& operator=(
            const FightRequestDelayFramesHook&) = delete;

        struct RawFString
        {
            wchar_t* data = nullptr;
            int32_t num = 0;
            int32_t max = 0;
        };
        static_assert(sizeof(RawFString) == 16, "FString layout drifted");

        struct LuxDataValue
        {
            std::byte bytes[0x18]{};
        };
        static_assert(sizeof(LuxDataValue) == 0x18, "LuxDataTableValue size");

        struct LuxDataTableRef
        {
            void* vtable = nullptr;
            std::byte path[0x18]{};
            void* owned_text = nullptr;
            int32_t text_num = 0;
            int32_t text_max = 0;
        };
        static_assert(sizeof(LuxDataTableRef) == 0x30, "LuxDataTableRef size");

        using BuildSpinBoxFn = LuxDataTableRef*(__fastcall*)(
            void* context,
            LuxDataTableRef* out,
            RawFString* name,
            RawFString* loc_label_text_id,
            RawFString* description,
            RawFString* data_key,
            uint8_t disabled);
        using AppendSpinBoxItemFn = LuxDataTableRef*(__fastcall*)(
            LuxDataTableRef* spinbox,
            RawFString* loc_text_id,
            LuxDataValue* value);
        using AppendArrayRowFn = LuxDataTableRef*(__fastcall*)(
            LuxDataTableRef* array_ref,
            LuxDataTableRef* out,
            LuxDataTableRef* row_value);
        using MakeIntValueFn = LuxDataValue*(__fastcall*)(LuxDataValue* out,
                                                         int32_t value);
        using FStringReserveFn = void(__fastcall*)(RawFString* str);
        using FMemoryFreeFn = void(__fastcall*)(void* ptr);
        using LuxDataTablePathDtorFn = void(__fastcall*)(void* path);
        using AppendStringFn = LuxDataTableRef*(__fastcall*)(
            LuxDataTableRef* path,
            LuxDataTableRef* out,
            RawFString* key);
        using ResolveFn = void*(__fastcall*)(LuxDataTableRef* path,
                                             void* out_path);
        using CompareStringFn = uint64_t(__fastcall*)(void* resolved_path,
                                                      RawFString* value);

        static constexpr uintptr_t kBuildFightRequestMenuRVA = 0x88E160;
        static constexpr uintptr_t kBuildSpinBoxRVA = 0x86B2D0;
        static constexpr uintptr_t kAppendSpinBoxItemRVA = 0x8ABF50;
        static constexpr uintptr_t kAppendArrayRowRVA = 0x2F4A980;
        static constexpr uintptr_t kMakeIntValueRVA = 0x2ED0CC0;
        static constexpr uintptr_t kFStringReserveRVA = 0x1EFE660;
        static constexpr uintptr_t kFMemoryFreeRVA = 0xD46A00;
        static constexpr uintptr_t kLuxDataTablePathDtorRVA = 0x2ED6A80;
        static constexpr uintptr_t kLuxDataTablePathAppendStringRVA = 0x2EDA150;
        static constexpr uintptr_t kLuxDataTableResolveRVA = 0x2F2EE30;
        static constexpr uintptr_t kLuxDataTableCompareStringRVA = 0x2ED9D50;

        static void resolve_helpers(uintptr_t base)
        {
            s_build_spinbox = reinterpret_cast<BuildSpinBoxFn>(
                base + kBuildSpinBoxRVA);
            s_append_spinbox_item = reinterpret_cast<AppendSpinBoxItemFn>(
                base + kAppendSpinBoxItemRVA);
            s_append_array_row = reinterpret_cast<AppendArrayRowFn>(
                base + kAppendArrayRowRVA);
            s_make_int_value = reinterpret_cast<MakeIntValueFn>(
                base + kMakeIntValueRVA);
            s_fstring_reserve = reinterpret_cast<FStringReserveFn>(
                base + kFStringReserveRVA);
            s_fmemory_free = reinterpret_cast<FMemoryFreeFn>(
                base + kFMemoryFreeRVA);
            s_path_dtor = reinterpret_cast<LuxDataTablePathDtorFn>(
                base + kLuxDataTablePathDtorRVA);
            s_append_string = reinterpret_cast<AppendStringFn>(
                base + kLuxDataTablePathAppendStringRVA);
            s_resolve = reinterpret_cast<ResolveFn>(
                base + kLuxDataTableResolveRVA);
            s_compare_string = reinterpret_cast<CompareStringFn>(
                base + kLuxDataTableCompareStringRVA);
        }

        static bool helpers_ready()
        {
            return s_build_spinbox && s_append_spinbox_item && s_append_array_row &&
                   s_make_int_value && s_fstring_reserve && s_fmemory_free &&
                   s_path_dtor && s_append_string && s_resolve && s_compare_string;
        }

        static bool equals_string(const RawFString* str, const wchar_t* text)
        {
            return str && str->data && std::wcscmp(str->data, text) == 0;
        }

        static void free_string(RawFString* str)
        {
            if (str && str->data)
            {
                s_fmemory_free(str->data);
                str->data = nullptr;
                str->num = 0;
                str->max = 0;
            }
        }

        static RawFString make_string(const wchar_t* text)
        {
            RawFString str{};
            const size_t len = std::wcslen(text) + 1;
            str.num = static_cast<int32_t>(len);
            str.max = 0;
            s_fstring_reserve(&str);
            if (str.data)
            {
                std::memcpy(str.data, text, len * sizeof(wchar_t));
            }
            return str;
        }

        static void cleanup_ref(LuxDataTableRef& ref)
        {
            if (ref.owned_text)
            {
                s_fmemory_free(ref.owned_text);
                ref.owned_text = nullptr;
            }
            s_path_dtor(ref.path);
            ref = {};
        }

        static bool row_name_is(LuxDataTableRef* row, const wchar_t* wanted)
        {
            if (!row || !wanted) return false;

            LuxDataTableRef name_path{};
            RawFString key = make_string(L"name");
            s_append_string(row, &name_path, &key);
            free_string(&key);

            std::byte resolved[0x18]{};
            s_resolve(&name_path, resolved);

            RawFString wanted_value = make_string(wanted);
            const bool match = s_compare_string(resolved, &wanted_value) != 0;
            free_string(&wanted_value);

            s_path_dtor(resolved);
            cleanup_ref(name_path);
            return match;
        }

        static LuxDataTableRef* build_delay_row(void* context,
                                                LuxDataTableRef* out,
                                                BuildSpinBoxFn build_spinbox)
        {
            RawFString name = make_string(L"delayFrames");
            RawFString label = make_string(L"ID_SYS_Paus_ITM_2300");
            RawFString description = make_string(L"ID_SYS_Paus_ITM_2301");
            RawFString key = make_string(L"delayFrames");

            LuxDataTableRef* result = build_spinbox(context, out, &name, &label,
                                                    &description, &key, 0);

            constexpr std::array<const wchar_t*, 5> kValueTextIds = {
                L"ID_SYS_OPM_Set_0042",
                L"ID_SYS_OPM_Set_0043",
                L"ID_SYS_OPM_Set_0044",
                L"ID_SYS_OPM_Set_0045",
                L"ID_SYS_OPM_Set_0046",
            };

            for (int32_t i = 0; i < static_cast<int32_t>(kValueTextIds.size()); ++i)
            {
                LuxDataValue value{};
                s_make_int_value(&value, i + 1);
                RawFString loc_text = make_string(kValueTextIds[static_cast<size_t>(i)]);
                s_append_spinbox_item(result ? result : out, &loc_text, &value);
            }
            return result ? result : out;
        }

        static LuxDataTableRef* __fastcall build_spinbox_detour(
            void* context,
            LuxDataTableRef* out,
            RawFString* name,
            RawFString* loc_label_text_id,
            RawFString* description,
            RawFString* data_key,
            uint8_t disabled)
        {
            BuildSpinBoxFn orig = reinterpret_cast<BuildSpinBoxFn>(
                instance().m_build_spinbox_trampoline);
            if (!orig) return out;

            if (!equals_string(name, L"requestSide"))
            {
                return orig(context, out, name, loc_label_text_id, description,
                            data_key, disabled);
            }

            LuxDataTableRef* result = build_delay_row(context, out, orig);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[FightRequestDelayFramesHook] replaced Preferred Side "
                    "with Frame Delay row\n"));
            return result;
        }

        static inline BuildSpinBoxFn s_build_spinbox = nullptr;
        static inline AppendSpinBoxItemFn s_append_spinbox_item = nullptr;
        static inline AppendArrayRowFn s_append_array_row = nullptr;
        static inline MakeIntValueFn s_make_int_value = nullptr;
        static inline FStringReserveFn s_fstring_reserve = nullptr;
        static inline FMemoryFreeFn s_fmemory_free = nullptr;
        static inline LuxDataTablePathDtorFn s_path_dtor = nullptr;
        static inline AppendStringFn s_append_string = nullptr;
        static inline ResolveFn s_resolve = nullptr;
        static inline CompareStringFn s_compare_string = nullptr;

        std::atomic<bool> m_installed{false};
        uint64_t m_build_spinbox_trampoline = 0;
        std::unique_ptr<PLH::x64Detour> m_build_spinbox_detour;
    };
}
