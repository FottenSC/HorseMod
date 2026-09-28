#include "Sc6ReplayWorld.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bit>
#include <cstring>
#include <intrin.h>

namespace Horse::Deterministic
{
namespace
{
template<class T> T& Field(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
}
template<class R = void, class... A>
R Native(std::uintptr_t base, std::uintptr_t rva, A... args)
{
    return reinterpret_cast<R(__fastcall*)(A...)>(base + rva)(args...);
}
template<class R = void, class... A>
R Virtual(void* object, std::size_t slot, A... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, A...)>(
        Field<std::uintptr_t>(Field<void*>(object, 0), slot))(object, args...);
}
float WrapTime(std::uintptr_t base, float time)
{
    auto* image = reinterpret_cast<void*>(base);
    const auto period = Field<float>(image, 0x3907c74);
    const auto mask = Field<std::uint32_t>(image, 0x3255650);
    auto absolute = [mask](float value) {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & mask);
    };
    if (!(absolute(period) > Field<float>(image, 0x326c2e0)))
    {
        Native(base, 0x2d2bc0);
        return 0.0f;
    }
    // CVTTSS2SI's overflow/NaN result differs from an undefined C++ cast.
    const auto whole = _mm_cvttss_si32(_mm_set_ss(time * Field<float>(image, 0x3907c70)));
    auto product = static_cast<float>(whole) * period;
    if (absolute(product) > absolute(time)) product = time;
    return time - product;
}
}

void Sc6ReplayWorld::NotifyListener(std::size_t slot)
{
    auto* engine = Field<void*>(reinterpret_cast<void*>(base_), 0x43b3068);
    if (auto* listener = Field<void*>(engine, 0x9b8))
        Virtual(listener, slot, Native<void*>(base_, 0x21784a0, engine, world_));
}

void Sc6ReplayWorld::Enter()
{
    token_ = Native<std::uint64_t>(base_, 0x1ee8d80);
    Native(base_, 0x1ee8f90, reinterpret_cast<void*>(base_ + 0x4097000), tick_type_, original_delta_);
    Native(base_, 0x1e8ff30, Field<void*>(world_, 0x9c8));
    Native(base_, 0x1e8ff30, Field<std::byte*>(world_, 0x9c8) + 0x60);
    NotifyListener(0x1d0);
    settings_ = Native<void*>(base_, 0x21bc760, world_, 0, true);

    std::byte factory_argument{};
    void* factory[2]{reinterpret_cast<void*>(base_ + 0x450440), &factory_argument};
    auto* tls = Native<std::byte*>(base_, 0xd47210, factory,
        reinterpret_cast<std::uint32_t*>(base_ + 0x406e788));
    mark_.arena = reinterpret_cast<MemoryArena*>(tls + 0x10);
    // Only the native mark's scope uses replay-owned chunks. Entry work above
    // and GC/render work after CloseMemory retain the ambient native arena.
    // Preserve +24's native policy value; its meaning is not yet established.
    owned_arena_ = {};
    owned_arena_.unknown = mark_.arena->unknown;
    AttachArena();
    mark_.top = mark_.arena->top;
    mark_.chunk = mark_.arena->chunk;
    mark_.previous = mark_.arena->mark;
    mark_.closed = false;
    mark_.arena->mark = &mark_;
    ++mark_.arena->marks;
    if (arena_probe_pending_)
    {
        arena_probe_pending_ = false;
        arena_probe_ = Native<std::byte*>(base_, 0x20354d0, 1, mark_.arena, 64, 16);
        std::memset(arena_probe_, 0xa5, 64);
    }
    Native(base_, 0x2d2bc0);
    Field<std::uint8_t>(world_, 0x780) = 1;
    paused_ = Native<bool>(base_, 0x1ef83d0, world_);
    Native(base_, 0x10a1800, world_ + 0x540, original_delta_);
    auto* net = Field<void*>(world_, 0x38);
    if (net && Field<void*>(net, 0x78)) Native(base_, 0x1f02f50, world_, original_delta_);
}

void Sc6ReplayWorld::Prepare()
{
    auto* image = reinterpret_cast<void*>(base_);
    Field<float>(world_, 0x938) = WrapTime(base_, original_delta_ + Field<float>(world_, 0x938));
    if (!paused_) Field<float>(world_, 0x93c) = original_delta_ + Field<float>(world_, 0x93c);
    // Assembly 141F0244C..476 multiplies the three dilations before delta.
    const float dilation = (Field<float>(settings_, 0x4e4) * Field<float>(settings_, 0x4e0))
        * Field<float>(settings_, 0x4e8);
    delta_ = Virtual<float>(settings_, 0x608, original_delta_ * dilation, original_delta_);
    Field<float>(world_, 0x940) = delta_;
    Field<float>(world_, 0x934) = delta_ + Field<float>(world_, 0x934);
    if (!paused_) Field<float>(world_, 0x930) = WrapTime(base_, delta_ + Field<float>(world_, 0x930));
    if (Field<std::uint8_t>(world_, 0x9c0) & 0x80) tick_type_ = 1;
    if ((Field<std::uint8_t>(settings_, 0x508) & 3) || Native<bool>(base_, 0x21be340, world_))
        Native(base_, 0xe5aeb0, 1, true, Field<float>(image, 0x4093d2c) * Field<float>(image, 0x3e89fd4));
    if (std::memcmp(world_ + 0x948, world_ + 0x954, 12) == 0)
        std::memcpy(world_ + 0x960, reinterpret_cast<void*>(base_ + 0x418ac48), 12);
    else
    {
        std::int32_t origin[3];
        std::memcpy(origin, world_ + 0x954, sizeof(origin));
        Native(base_, 0x21c5380, world_, origin);
    }
    if (!paused_)
        if (auto* ai = Field<void*>(world_, 0xe8)) Virtual(ai, 0x230, delta_);
    auto* net = Field<void*>(world_, 0x38);
    auto* connection = net ? Field<void*>(net, 0x78) : nullptr;
    run_tasks_ = tick_type_ != 0 && !paused_ && (!connection || Field<int>(connection, 0x124) == 3);
    auto* instance = Field<void*>(world_, 0x140);
    latent_ = instance ? Field<void*>(instance, 0xe0) : world_ + 0x438;

    // Reset the processed-object set while retaining its native allocation.
    auto* set = static_cast<std::byte*>(latent_) + 0x50;
    Field<int>(set, 8) = 0;
    if (Field<int>(set, 0xc) < 0) Native(base_, 0x2025710, set, 0);
    Field<int>(set, 0x34) = 0;
    Field<int>(set, 0x30) = -1;
    const int rounded = Field<int>(set, 0x28) + 31;
    const auto words = (rounded + ((rounded >> 31) & 31)) >> 5;
    auto* bits = Field<void*>(set, 0x20);
    std::memset(bits ? bits : set + 0x10, 0, static_cast<std::size_t>(words) * 4);
    Field<int>(set, 0x28) = 0;
    const auto hash_size = Field<int>(set, 0x48);
    auto* hash = Field<int*>(set, 0x40);
    if (!hash) hash = reinterpret_cast<int*>(set + 0x38);
    for (int i = 0; i < hash_size; ++i) hash[i & (hash_size - 1)] = -1;
    if (run_tasks_) Native(base_, 0x1d5a5f0, world_);
    collection_ = Field<std::byte*>(world_, 0x120);
    collection_end_ = collection_ + static_cast<std::ptrdiff_t>(Field<int>(world_, 0x128)) * 0x80;
}

void Sc6ReplayWorld::OpenCollection()
{
    levels_ = {};
    // FLevelCollection +28 is a sparse set: 16-byte elements, allocation
    // flags inline at +38 or heap at +48, allocation bit count at +50.
    auto* bits = Field<std::uint32_t*>(collection_, 0x48);
    if (!bits) bits = reinterpret_cast<std::uint32_t*>(collection_ + 0x38);
    const int count = Field<int>(collection_, 0x50);
    auto* elements = Field<std::byte*>(collection_, 0x28);
    for (int index = 0; index < count; ++index)
    {
        if (!(bits[index >> 5] & (1u << (index & 31)))) continue;
        auto* level = Field<void*>(elements, static_cast<std::size_t>(index) * 16);
        auto** loaded = Field<void**>(world_, 0x110);
        for (int j = 0; j < Field<int>(world_, 0x118); ++j)
        {
            if (loaded[j] != level) continue;
            const auto old_count = levels_.count++;
            if (levels_.count > levels_.capacity) Native(base_, 0x15d0bd0, &levels_, old_count);
            static_cast<void**>(levels_.data)[old_count] = level;
            break;
        }
    }
    Native<void*>(base_, 0x21b4000, collection_context_, collection_, world_);
}

bool Sc6ReplayWorld::AdvanceGroup(int group, bool wait)
{
    if (task_groups_.idle()) task_groups_.Begin(base_, Native<void*>(base_, 0x215f460), group, wait);
    task_groups_.Advance();
    if (!task_groups_.idle()) return false;
    ++Field<int>(world_, 0x784);
    return true;
}

void Sc6ReplayWorld::TickCollectionConsumers()
{
    if (Field<int>(collection_, 0) != 0) return;
    if (!paused_) Native(base_, 0x1ed7750, latent_, static_cast<void*>(nullptr), delta_);
    if (tick_type_ && !paused_)
        Native(base_, 0x2187b20, Native<void*>(base_, 0x1ef4c40, world_), delta_);
    Native(base_, 0x1f03080, world_, tick_type_, paused_, delta_);
    struct Iterator { Array* array; int index; int padding; } iterator{};
    Native<void*>(base_, 0x21bc3e0, world_, &iterator);
    while (iterator.index >= 0 && iterator.index < iterator.array->count)
    {
        auto* weak = static_cast<std::byte*>(iterator.array->data) + iterator.index * 8;
        auto* controller = Native<void*>(base_, 0xf823f0, weak);
        if (!paused_ || Native<bool>(base_, 0x20546c0, controller)) Virtual(controller, 0xc48, delta_);
        else if (auto* camera = Field<void*>(controller, 0x420))
            if (Native<bool>(base_, 0x1d21cb0, world_)) Native(base_, 0x20577d0, camera);
        ++iterator.index;
    }
    if (!paused_ && Native<bool>(base_, 0x21be320, world_))
    {
        Native(base_, 0x1efad70, world_, 0);
        if (auto* tail = Field<void*>(world_, 0x970)) Native(base_, 0x21c8c30, tail);
    }
}

void Sc6ReplayWorld::TickTail()
{
    if (run_tasks_)
    {
        if (Field<void*>(world_, 0x1c8))
            Native(base_, 0x2018d90, Field<void*>(reinterpret_cast<void*>(base_), 0x43a46b8));
        Native(base_, 0x1d45520, world_);
    }
    Native(base_, 0x10a1800, world_ + 0x5b0, original_delta_);
    Native(base_, 0x3999c0, world_ + 0x620);
    if (auto* timed = Field<void*>(world_, 0x168))
        Virtual(timed, 0x190, static_cast<double>(Field<float>(world_, 0x930)));
    if (!paused_)
        if (auto* tail = Field<void*>(world_, 0x770)) Virtual(tail, 0, delta_);
    auto& flags = Field<std::uint32_t>(world_, 0x9c0);
    if (flags & 0x8000) flags = (flags & ~0x8000u) | 0x1000;
    Field<std::uint8_t>(world_, 0x780) = 0;
}

void Sc6ReplayWorld::AttachArena()
{
    if (arena_attached_ || !mark_.arena) __fastfail(FAST_FAIL_INVALID_ARG);
    ambient_arena_ = *mark_.arena;
    *mark_.arena = owned_arena_;
    arena_attached_ = true;
}

void Sc6ReplayWorld::DetachArena()
{
    if (!arena_attached_) return;
    if (arena_probe_)
    {
        for (int i = 0; i < 64; ++i)
            if (arena_probe_[i] != std::byte{0xa5}) __fastfail(FAST_FAIL_INVALID_ARG);
        ++arena_probe_checks_;
    }
    // No native nested mark may escape a completed task/phase. Our mark must
    // have no ambient ancestor, including when the ambient caller has a mark.
    if (!mark_.closed && (mark_.arena->mark != &mark_ || mark_.arena->marks != 1
        || mark_.previous || mark_.top || mark_.chunk)) __fastfail(FAST_FAIL_INVALID_ARG);
    owned_arena_ = *mark_.arena;
    std::uint64_t bytes = 0;
    for (auto* chunk = owned_arena_.chunk; chunk; chunk = Field<void*>(chunk, 0))
    {
        const auto size = Field<int>(chunk, 8);
        if (size < 0) __fastfail(FAST_FAIL_INVALID_ARG);
        bytes += static_cast<std::uint64_t>(size) + 0x10;
    }
    if (bytes > max_retained_arena_bytes_) max_retained_arena_bytes_ = bytes;
    ++arena_scopes_;
    *mark_.arena = ambient_arena_;
    ambient_arena_ = {};
    arena_attached_ = false;
}

void Sc6ReplayWorld::CloseMemory()
{
    if (mark_.closed) return;
    if (!arena_attached_ || mark_.arena->mark != &mark_ || mark_.arena->marks != 1)
        __fastfail(FAST_FAIL_INVALID_ARG);
    mark_.closed = true;
    --mark_.arena->marks;
    if (mark_.chunk != mark_.arena->chunk) Native(base_, 0xdc6c50, mark_.arena, mark_.chunk);
    arena_probe_ = nullptr; // The native pop above ends this allocation's lifetime.
    mark_.arena->top = mark_.top;
    mark_.arena->mark = mark_.previous;
    mark_.top = nullptr;
    DetachArena();
}

void Sc6ReplayWorld::CollectGarbage()
{
    auto* image = reinterpret_cast<void*>(base_);
    auto& force = Field<bool>(world_, 0x89c);
    auto& elapsed = Field<float>(world_, 0x898);
    if (force)
    {
        if (Native<bool>(base_, 0xf44800, 0, true))
        {
            Native(base_, 0x1eea990, world_);
            force = false;
            elapsed = 0.0f;
        }
    }
    else if (Native<bool>(base_, 0x21bceb0, world_))
    {
        elapsed = delta_ + elapsed;
        const float threshold = Field<float>(image, 0x4094854);
        auto& skip = Field<bool>(world_, 0x89d);
        if (skip) skip = false;
        else if (Native<bool>(base_, 0xf38650) || elapsed <= threshold || threshold <= 0.0f)
            Native(base_, 0xf378c0, true, Field<float>(image, 0x3e89fe4));
        else if (!Field<bool(*)()>(image, 0x40713a8)() && Native<bool>(base_, 0xf44800, 0, false))
        {
            Native(base_, 0x1eea990, world_);
            elapsed = 0.0f;
        }
    }
    if (*Field<int*>(image, 0x439b6f0) > 0)
    {
        elapsed = Field<float>(image, 0x4094854) + Field<float>(image, 0x3e8a31c);
        force = true;
    }
    auto& flags = Field<std::uint32_t>(world_, 0x9c0);
    if (flags & 0x100) flags = ((flags >> 1 ^ flags) & 0x80 ^ flags) & ~0x100u;
    Field<int>(world_, 0xd8) = 0;
    if (Field<int>(world_, 0xdc) < 0) Native(base_, 0x1d27730, world_ + 0xd0, 0);
}

void Sc6ReplayWorld::DispatchRender()
{
    auto* image = reinterpret_cast<void*>(base_);
    if (!Field<bool>(image, 0x4351840) && (!Field<bool>(image, 0x419718c)
        || GetCurrentThreadId() == Field<std::uint32_t>(image, 0x419716c)))
    {
        Native(base_, 0x15edaf0);
        Native(base_, 0x1e8ff30, Field<std::byte*>(world_, 0x9c8) + 0x30);
        return;
    }
    std::uintptr_t storage[3]{};
    auto* builder = Native<std::uintptr_t*>(base_, 0x1eecc50, storage, 0, 0xff);
    auto* task = reinterpret_cast<void*>(builder[0]);
    Field<void*>(task, 0x10) = world_;
    auto* event = Field<void*>(task, 0x20);
    if (event) InterlockedIncrement(&Field<LONG>(event, 0x48));
    Native(base_, 0x134c6c0, task, reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);
    if (event && InterlockedDecrement(&Field<LONG>(event, 0x48)) == 0) Native(base_, 0xd2ebe0, event);
}

void Sc6ReplayWorld::Begin(std::uintptr_t base, void* world, int tick_type, float delta)
{
    // A recursive entry cannot replace a live continuation. Do not forward the
    // original world after partially executing its replacement.
    if (!idle() || !arena_empty()) __fastfail(FAST_FAIL_INVALID_ARG);
    base_ = base;
    world_ = static_cast<std::byte*>(world);
    tick_type_ = tick_type;
    original_delta_ = delta;
    phase_ = Phase::Entry;
}

void Sc6ReplayWorld::SyncFrame(std::uintptr_t base, void* state, bool allow_one_frame_lag)
{
    if (!idle() || !arena_empty()) __fastfail(FAST_FAIL_INVALID_ARG);
    task_groups_.SyncFrame(base, state, allow_one_frame_lag);
}

void Sc6ReplayWorld::Advance()
{
    if (!mark_.closed) AttachArena();
    switch (phase_)
    {
    case Phase::Entry: Enter(); phase_ = Phase::Prepare; break;
    case Phase::Prepare: Prepare(); phase_ = Phase::Collection; break;
    case Phase::Collection:
        if (collection_ == collection_end_) { phase_ = Phase::WorldTail; break; }
        OpenCollection(); phase_ = Phase::StartTasks; break;
    case Phase::StartTasks:
        if (run_tasks_)
        {
            Native(base_, 0x2027390, world_, delta_);
            Field<int>(world_, 0x784) = 0;
            if (task_groups_.BeginPriorCleanup(base_, Native<void*>(base_, 0x215f460), world_, delta_, tick_type_)) {
                phase_ = Phase::PriorCleanup;
                break;
            }
            Virtual(Native<void*>(base_, 0x215f460), 0x18, world_, delta_, tick_type_, &levels_);
            phase_ = Phase::Group0;
        }
        else
        {
            if (paused_) Virtual(Native<void*>(base_, 0x215f460), 0x20, world_, delta_, 3, &levels_);
            phase_ = Phase::CollectionConsumers;
        }
        break;
    case Phase::PriorCleanup:
        if (task_groups_.AdvancePriorCleanup()) {
            Virtual(Native<void*>(base_, 0x215f460), 0x18, world_, delta_, tick_type_, &levels_);
            phase_ = Phase::Group0;
        }
        break;
    case Phase::Group0: if (AdvanceGroup(0, true)) phase_ = Phase::AfterGroup0; break;
    case Phase::AfterGroup0:
        Field<std::uint8_t>(world_, 0x780) = 0;
        Native(base_, 0x21b9dd0, world_);
        Field<std::uint8_t>(world_, 0x780) = 1;
        phase_ = Phase::Group1; break;
    case Phase::Group1: if (AdvanceGroup(1, true)) phase_ = Phase::Group2; break;
    case Phase::Group2:
        if (!AdvanceGroup(2, false)) break;
        Field<int>(world_, 0x784) = 3; // Native assigns rather than incrementing here.
        phase_ = Phase::Group3; break;
    case Phase::Group3: if (AdvanceGroup(3, true)) phase_ = Phase::Group4; break;
    case Phase::Group4: if (AdvanceGroup(4, true)) phase_ = Phase::CollectionConsumers; break;
    case Phase::CollectionConsumers:
        TickCollectionConsumers(); phase_ = run_tasks_ ? Phase::Group5 : Phase::FinishCollection; break;
    case Phase::Group5: if (AdvanceGroup(5, true)) phase_ = Phase::Group6; break;
    case Phase::Group6: if (AdvanceGroup(6, true)) phase_ = Phase::FinishTasks; break;
    case Phase::FinishTasks:
        Virtual(Native<void*>(base_, 0x215f460), 0x30); phase_ = Phase::FinishCollection; break;
    case Phase::FinishCollection:
        Native(base_, 0x21b4bd0, collection_context_);
        if (levels_.data) Native(base_, 0xd46a00, levels_.data);
        levels_ = {};
        collection_ += 0x80; phase_ = Phase::Collection; break;
    case Phase::WorldTail: TickTail(); phase_ = Phase::CloseMemory; break;
    case Phase::CloseMemory: CloseMemory(); phase_ = Phase::CollectGarbage; break;
    case Phase::CollectGarbage: CollectGarbage(); phase_ = Phase::NotifyEnd; break;
    case Phase::NotifyEnd: NotifyListener(0x1d8); phase_ = Phase::Render; break;
    case Phase::Render: DispatchRender(); phase_ = Phase::Finish; break;
    case Phase::Finish:
        Native(base_, 0x1eef1d0, token_); CloseMemory(); phase_ = Phase::Idle; break;
    case Phase::Idle: break;
    }
    DetachArena();
}
}
