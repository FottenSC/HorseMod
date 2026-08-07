// ============================================================================
// Horse::RollbackReplaySelectionAvailabilityHook
//
// Replay-corpus-only stock availability adapter.  The native filters run
// before rollback owns battle simulation, so an unavailable replay-authored
// character/stage otherwise prevents the two-client test from reaching the
// battle.  The detours call stock first and append only the exact, bilaterally
// authenticated replay selection.  They never alter Steam ownership state,
// asset loading, save data, ordinary character select, or production beta
// configuration.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "RollbackReplaySelectionAvailabilityPolicy.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <polyhook2/Detour/x64Detour.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace Horse
{
    class RollbackReplaySelectionAvailabilityHook
    {
    public:
        struct Configuration
        {
            RollbackReplaySelectionAvailabilityPolicyInput policy;
        };

        struct Report
        {
            RollbackReplaySelectionAvailabilityDecision decision {
                RollbackReplaySelectionAvailabilityDecision::Disabled};
            bool installed {false};
            bool active {false};
            uint64_t character_filter_calls {0};
            uint64_t stage_filter_calls {0};
            uint8_t character_seen_mask {0};
            uint8_t character_appended_mask {0};
            bool stage_seen {false};
            bool stage_appended {false};
            uint64_t append_failures {0};

            bool character_acknowledged() const noexcept
            {
                return (character_seen_mask & 0x3u) == 0x3u;
            }

            bool stage_acknowledged() const noexcept
            {
                return stage_seen;
            }
        };

        static RollbackReplaySelectionAvailabilityHook& instance() noexcept
        {
            static RollbackReplaySelectionAvailabilityHook s_instance;
            return s_instance;
        }

        bool configure(Configuration configuration) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_policy = std::move(configuration.policy);
            m_policy.peer_contract_ready = false;
            m_policy.setup_complete = false;
            clear_evidence_locked();
            const auto decision =
                EvaluateRollbackReplaySelectionAvailability(m_policy);
            m_decision.store(decision, std::memory_order_release);
            m_active.store(false, std::memory_order_release);
            if (decision
                    == RollbackReplaySelectionAvailabilityDecision::Disabled)
                return true;
            if (decision
                    == RollbackReplaySelectionAvailabilityDecision::
                        InvalidScope
                || decision
                    == RollbackReplaySelectionAvailabilityDecision::
                        SelectionMismatch)
                return false;
            if (!copy_ascii_code(
                    m_policy.left_character_code, m_left_character)
                || !copy_ascii_code(
                    m_policy.right_character_code, m_right_character)
                || !copy_ascii_code(m_policy.stage_code, m_stage))
            {
                m_decision.store(
                    RollbackReplaySelectionAvailabilityDecision::InvalidScope,
                    std::memory_order_release);
                return false;
            }
            return install_locked();
        }

        void update_peer_contract(bool ready, bool setup_complete) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_policy.peer_contract_ready = ready;
            m_policy.setup_complete = setup_complete;
            const auto decision =
                EvaluateRollbackReplaySelectionAvailability(m_policy);
            m_decision.store(decision, std::memory_order_release);
            m_active.store(
                decision
                    == RollbackReplaySelectionAvailabilityDecision::Active
                    && m_installed.load(std::memory_order_acquire),
                std::memory_order_release);
        }

        void deactivate() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_policy = {};
            m_decision.store(
                RollbackReplaySelectionAvailabilityDecision::Disabled,
                std::memory_order_release);
            m_active.store(false, std::memory_order_release);
        }

        Report report() const noexcept
        {
            Report out {};
            out.decision = m_decision.load(std::memory_order_acquire);
            out.installed = m_installed.load(std::memory_order_acquire);
            out.active = m_active.load(std::memory_order_acquire);
            out.character_filter_calls =
                m_character_filter_calls.load(std::memory_order_acquire);
            out.stage_filter_calls =
                m_stage_filter_calls.load(std::memory_order_acquire);
            out.character_seen_mask =
                m_character_seen_mask.load(std::memory_order_acquire);
            out.character_appended_mask =
                m_character_appended_mask.load(std::memory_order_acquire);
            out.stage_seen = m_stage_seen.load(std::memory_order_acquire);
            out.stage_appended =
                m_stage_appended.load(std::memory_order_acquire);
            out.append_failures =
                m_append_failures.load(std::memory_order_acquire);
            return out;
        }

    private:
        struct RawFString
        {
            wchar_t* pData {nullptr};
            int32_t nCount {0};
            int32_t nCapacity {0};
        };
        static_assert(sizeof(RawFString) == 0x10);

        struct RawFStringArray
        {
            RawFString* pData {nullptr};
            int32_t nCount {0};
            int32_t nCapacity {0};
        };
        static_assert(sizeof(RawFStringArray) == 0x10);

        struct FixedWideCode
        {
            std::array<wchar_t, 16> text {};
            int32_t count_with_null {0};
        };

        using FilterCodesFn = RawFStringArray*(__fastcall*)(
            void*, RawFStringArray*, RawFStringArray*);
        using GrowArray16Fn = void(__fastcall*)(RawFStringArray*);
        using CopyFStringFn = RawFString*(__fastcall*)(
            RawFString*, const RawFString*);

        static constexpr uintptr_t kFilterCharacterCodesRva = 0x638290;
        static constexpr uintptr_t kFilterStageCodesRva = 0x6409F0;
        static constexpr uintptr_t kGrowArray16Rva = 0x1ACEF90;
        static constexpr uintptr_t kCopyFStringRva = 0x4093D0;

        RollbackReplaySelectionAvailabilityHook() = default;
        RollbackReplaySelectionAvailabilityHook(
            const RollbackReplaySelectionAvailabilityHook&) = delete;
        RollbackReplaySelectionAvailabilityHook& operator=(
            const RollbackReplaySelectionAvailabilityHook&) = delete;

        static bool copy_ascii_code(
            const std::string& source, FixedWideCode& destination) noexcept
        {
            destination = {};
            if (source.empty()
                || source.size() + 1 > destination.text.size())
                return false;
            for (size_t i = 0; i < source.size(); ++i)
            {
                const unsigned char c =
                    static_cast<unsigned char>(source[i]);
                if (c < 0x20 || c > 0x7e) return false;
                destination.text[i] = static_cast<wchar_t>(c);
            }
            destination.text[source.size()] = L'\0';
            destination.count_with_null =
                static_cast<int32_t>(source.size() + 1);
            return true;
        }

        static bool matches_bytes(
            uintptr_t address, const uint8_t* expected,
            size_t size) noexcept
        {
            if (!address || !expected || size == 0) return false;
            __try
            {
                return std::memcmp(
                    reinterpret_cast<const void*>(address),
                    expected, size) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool signatures_valid_locked(uintptr_t base) noexcept
        {
            static constexpr uint8_t character_entry[] = {
                0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x54,
                0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x55,
            };
            static constexpr uint8_t stage_entry[] = {
                0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x54,
                0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x55,
            };
            static constexpr uint8_t grow_entry[] = {
                0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                0xec, 0x20, 0x48, 0x63, 0x79, 0x08, 0x48, 0x8b,
            };
            static constexpr uint8_t copy_entry[] = {
                0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x33,
            };
            return matches_bytes(base + kFilterCharacterCodesRva,
                                  character_entry, sizeof(character_entry))
                && matches_bytes(base + kFilterStageCodesRva,
                                 stage_entry, sizeof(stage_entry))
                && matches_bytes(base + kGrowArray16Rva,
                                 grow_entry, sizeof(grow_entry))
                && matches_bytes(base + kCopyFStringRva,
                                 copy_entry, sizeof(copy_entry));
        }

        bool install_locked() noexcept
        {
            if (m_installed.load(std::memory_order_acquire)) return true;
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || !signatures_valid_locked(base))
            {
                RC::Output::send<RC::LogLevel::Error>(STR(
                    "[RollbackReplaySelectionAvailabilityHook] native "
                    "availability signature mismatch; test override "
                    "disabled\n"));
                return false;
            }
            m_grow_array = reinterpret_cast<GrowArray16Fn>(
                base + kGrowArray16Rva);
            m_copy_fstring = reinterpret_cast<CopyFStringFn>(
                base + kCopyFStringRva);
            m_character_trampoline = 0;
            m_character_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(base + kFilterCharacterCodesRva),
                reinterpret_cast<uint64_t>(&detour_filter_character_codes),
                &m_character_trampoline);
            if (!m_character_detour->hook())
            {
                m_character_detour.reset();
                return false;
            }
            m_stage_trampoline = 0;
            m_stage_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(base + kFilterStageCodesRva),
                reinterpret_cast<uint64_t>(&detour_filter_stage_codes),
                &m_stage_trampoline);
            if (!m_stage_detour->hook())
            {
                m_stage_detour.reset();
                m_character_detour->unHook();
                m_character_detour.reset();
                m_character_trampoline = 0;
                return false;
            }
            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[RollbackReplaySelectionAvailabilityHook] installed "
                "test-only stock character/stage availability adapters\n"));
            return true;
        }

        static bool raw_string_equals(
            const RawFString& value, const FixedWideCode& expected) noexcept
        {
            if (!value.pData || value.nCount <= 0
                || value.nCapacity < value.nCount
                || expected.count_with_null <= 1)
                return false;
            const int32_t text_count = expected.count_with_null - 1;
            if (value.nCount != text_count
                && value.nCount != expected.count_with_null)
                return false;
            __try
            {
                for (int32_t i = 0; i < text_count; ++i)
                {
                    if (value.pData[i] != expected.text[i]) return false;
                }
                return value.nCount == text_count
                    || value.pData[text_count] == L'\0';
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static bool array_valid(const RawFStringArray* array) noexcept
        {
            if (!array) return false;
            __try
            {
                return array->nCount >= 0
                    && array->nCapacity >= array->nCount
                    && array->nCapacity <= 4096
                    && (array->nCount == 0 || array->pData != nullptr);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static bool array_contains(
            const RawFStringArray* array,
            const FixedWideCode& expected) noexcept
        {
            if (!array_valid(array)) return false;
            __try
            {
                for (int32_t i = 0; i < array->nCount; ++i)
                {
                    if (raw_string_equals(array->pData[i], expected))
                        return true;
                }
                return false;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool append_exact(
            RawFStringArray* array, const FixedWideCode& expected,
            bool& appended) noexcept
        {
            appended = false;
            if (!array_valid(array) || !m_grow_array || !m_copy_fstring)
                return false;
            if (array_contains(array, expected)) return true;
            int32_t original_count = 0;
            __try
            {
                original_count = array->nCount;
                const int32_t index = original_count;
                array->nCount = index + 1;
                if (array->nCapacity < array->nCount)
                    m_grow_array(array);
                if (!array->pData || array->nCapacity < array->nCount)
                {
                    array->nCount = index;
                    return false;
                }
                RawFString source {
                    const_cast<wchar_t*>(expected.text.data()),
                    expected.count_with_null,
                    expected.count_with_null,
                };
                if (!m_copy_fstring(&array->pData[index], &source))
                {
                    array->nCount = index;
                    return false;
                }
                appended = true;
                return raw_string_equals(array->pData[index], expected);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // A failed test-only append must not publish a partly
                // constructed FString as an array element. Capacity may have
                // grown, which is harmless and remains owned by the array.
                __try
                {
                    array->nCount = original_count;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
                return false;
            }
        }

        void apply_character_override(RawFStringArray* output) noexcept
        {
            if (!m_active.load(std::memory_order_acquire)) return;
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_active.load(std::memory_order_acquire)) return;
            m_character_filter_calls.fetch_add(1, std::memory_order_acq_rel);
            const FixedWideCode* codes[2] = {
                &m_left_character, &m_right_character};
            for (uint8_t index = 0; index < 2; ++index)
            {
                bool appended = false;
                const bool ok = append_exact(output, *codes[index], appended);
                if (ok && array_contains(output, *codes[index]))
                    m_character_seen_mask.fetch_or(
                        static_cast<uint8_t>(1u << index),
                        std::memory_order_acq_rel);
                else
                    m_append_failures.fetch_add(1, std::memory_order_acq_rel);
                if (appended)
                    m_character_appended_mask.fetch_or(
                        static_cast<uint8_t>(1u << index),
                        std::memory_order_acq_rel);
            }
        }

        void apply_stage_override(RawFStringArray* output) noexcept
        {
            if (!m_active.load(std::memory_order_acquire)) return;
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_active.load(std::memory_order_acquire)) return;
            m_stage_filter_calls.fetch_add(1, std::memory_order_acq_rel);
            bool appended = false;
            const bool ok = append_exact(output, m_stage, appended);
            if (ok && array_contains(output, m_stage))
                m_stage_seen.store(true, std::memory_order_release);
            else
                m_append_failures.fetch_add(1, std::memory_order_acq_rel);
            if (appended)
                m_stage_appended.store(true, std::memory_order_release);
        }

        static RawFStringArray* __fastcall detour_filter_character_codes(
            void* context, RawFStringArray* output,
            RawFStringArray* input) noexcept
        {
            auto& self = instance();
            RawFStringArray* result = output;
            const auto original = reinterpret_cast<FilterCodesFn>(
                self.m_character_trampoline);
            if (original) result = original(context, output, input);
            self.apply_character_override(result ? result : output);
            return result;
        }

        static RawFStringArray* __fastcall detour_filter_stage_codes(
            void* context, RawFStringArray* output,
            RawFStringArray* input) noexcept
        {
            auto& self = instance();
            RawFStringArray* result = output;
            const auto original = reinterpret_cast<FilterCodesFn>(
                self.m_stage_trampoline);
            if (original) result = original(context, output, input);
            self.apply_stage_override(result ? result : output);
            return result;
        }

        void clear_evidence_locked() noexcept
        {
            m_character_filter_calls.store(0, std::memory_order_release);
            m_stage_filter_calls.store(0, std::memory_order_release);
            m_character_seen_mask.store(0, std::memory_order_release);
            m_character_appended_mask.store(0, std::memory_order_release);
            m_stage_seen.store(false, std::memory_order_release);
            m_stage_appended.store(false, std::memory_order_release);
            m_append_failures.store(0, std::memory_order_release);
        }

        mutable std::mutex m_mutex;
        RollbackReplaySelectionAvailabilityPolicyInput m_policy {};
        FixedWideCode m_left_character {};
        FixedWideCode m_right_character {};
        FixedWideCode m_stage {};
        GrowArray16Fn m_grow_array {nullptr};
        CopyFStringFn m_copy_fstring {nullptr};
        std::unique_ptr<PLH::x64Detour> m_character_detour;
        std::unique_ptr<PLH::x64Detour> m_stage_detour;
        uint64_t m_character_trampoline {0};
        uint64_t m_stage_trampoline {0};
        std::atomic<bool> m_installed {false};
        std::atomic<bool> m_active {false};
        std::atomic<RollbackReplaySelectionAvailabilityDecision> m_decision {
            RollbackReplaySelectionAvailabilityDecision::Disabled};
        std::atomic<uint64_t> m_character_filter_calls {0};
        std::atomic<uint64_t> m_stage_filter_calls {0};
        std::atomic<uint8_t> m_character_seen_mask {0};
        std::atomic<uint8_t> m_character_appended_mask {0};
        std::atomic<bool> m_stage_seen {false};
        std::atomic<bool> m_stage_appended {false};
        std::atomic<uint64_t> m_append_failures {0};
    };
}
