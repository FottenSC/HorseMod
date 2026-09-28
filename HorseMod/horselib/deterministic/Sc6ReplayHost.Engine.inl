// 141E38F70 body, between UE4SS's unchanged pre/post callback dispatch.
// These phases currently drain; an exterior yield must also own ambient task
// admission and logical engine epochs before it may leave World unfinished.
namespace
{
template<class T> T& EngineField(void* object, std::size_t offset)
{ return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset); }
template<class R = void, class... A> R EngineNative(std::uintptr_t base, std::uintptr_t rva, A... args)
{ return reinterpret_cast<R(__fastcall*)(A...)>(base + rva)(args...); }
template<class R = void, class... A> R EngineVirtual(void* object, std::size_t slot, A... args)
{
    return reinterpret_cast<R(__fastcall*)(void*, A...)>(
        EngineField<std::uintptr_t>(EngineField<void*>(object, 0), slot))(object, args...);
}
}

void Sc6ReplayHost::EnterEngine()
{
    auto* image = reinterpret_cast<void*>(image_base_);
    if (auto* reload = EngineNative<void*>(image_base_, 0x39b9c0)) EngineVirtual(reload, 0x40);
    // Bind proved this native static initialized before replacing execution.
    if (!EngineField<bool>(image, 0x418b12f) && !EngineField<bool>(image, 0x42987e8))
        EngineNative(image_base_, 0x216fa20, engine_);
    if (EngineField<bool>(image, 0x4197133) && !EngineField<void*>(engine_, 0x618)
        && (!EngineField<bool>(image, 0x418b12f) || EngineField<bool>(image, 0x4197130))
        && !EngineField<bool>(image, 0x42987e8))
    {
        EngineNative(image_base_, 0xe0e590, false);
        engine_phase_ = EnginePhase::Finish;
        return;
    }
    if (auto* viewport = EngineField<void*>(engine_, 0x618)) EngineVirtual(viewport, 0x298, engine_delta_);
    EngineNative(image_base_, 0xf8ffb0, engine_delta_, EngineField<int>(image, 0x4093d28) != 0,
        EngineField<float>(image, 0x4093d24) * EngineField<float>(image, 0x3e89fd4));
    if (auto* subsystem = EngineField<void*>(image, 0x43954a8))
        EngineNative(image_base_, 0x1e38ce0, subsystem, engine_delta_);
    saved_context_handle_ = 0;
    auto** contexts = EngineField<std::byte**>(engine_, 0xbe8);
    for (int i = 0; i < EngineField<int>(engine_, 0xbf0); ++i)
        if (EngineField<void*>(contexts[i], 0x298) == EngineField<void*>(image, 0x43b4db8))
        {
            saved_context_handle_ = EngineField<std::uint64_t>(contexts[i], 0xc8);
            break;
        }
    context_index_ = 0;
    any_world_running_ = false;
    engine_phase_ = EnginePhase::Context;
}

bool Sc6ReplayHost::TickEngineWorld()
{
    if (executor_.idle())
    {
        if (idle_mode_) return true;
        EngineField<int>(engine_, 0x634) = 0;
        auto* world = EngineField<void*>(context_, 0x298);
        engine_world_bound_ = world == world_;
        if (engine_world_bound_ && paused_) { ++held_updates_; return true; }
        // All engine worlds share the named game-thread queues. A native tick
        // of another context can dequeue a protected consumer from our world.
        // Reuse this sequential executor so every pop retains its admission.
        executor_.Begin(image_base_, world, 2, engine_delta_);
    }
    // A native UWorld call retains its original world argument throughout.
    // Context changes during that call affect the later engine tail only.
    executor_.Advance();
    if (!executor_.idle()) return false;
    if (engine_world_bound_) ++completed_worlds_;
    return true;
}

void Sc6ReplayHost::DispatchCauseEvent()
{
    auto* world = EngineField<void*>(context_, 0x298);
    auto& flags = EngineField<std::uint32_t>(world, 0xe0);
    if (!(flags & 1)) return;
    flags &= ~1u;
    const auto* option = EngineNative<const wchar_t*>(image_base_, 0x21a1a20,
        context_ + 0xe8, L"causeevent=", static_cast<const wchar_t*>(nullptr));
    auto* instance = EngineField<void*>(context_, 0x240);
    auto* player = instance ? EngineNative<void*>(image_base_, 0x1e4f1f0, instance) : nullptr;
    if (option && player)
    {
        // Same temporary command text as the native FString concatenation.
        // The exec interface consumes it synchronously before destruction.
        std::wstring command = L"CAUSEEVENT ";
        command += option;
        auto* output = EngineNative<void*>(image_base_, 0xdc9bc0);
        auto* player_world = EngineVirtual<void*>(player, 0x138);
        EngineVirtual(static_cast<std::byte*>(player) + 0x28, 8, player_world, command.c_str(), output);
    }
    EngineField<std::uint32_t>(EngineField<void*>(context_, 0x298), 0xe0) |= 2;
}

void Sc6ReplayHost::TickEngineWorldTail()
{
    auto* image = reinterpret_cast<void*>(image_base_);
    if (!EngineField<bool>(image, 0x418b12f)
        && EngineNative<bool>(image_base_, 0x21b6a50, EngineField<void*>(context_, 0x298)))
    {
        EngineNative(image_base_, 0x1ddc040, EngineField<void*>(context_, 0x298));
        EngineNative(image_base_, 0x1db2610, EngineField<void*>(context_, 0x298));
    }
    DispatchCauseEvent();
    EngineNative(image_base_, 0x2189620, engine_, EngineField<void*>(context_, 0x298));
    if (EngineField<std::uint32_t>(EngineField<void*>(context_, 0x298), 0x9c0) & 8)
    {
        EngineNative(image_base_, 0x216e3c0, engine_);
        EngineField<std::uint32_t>(EngineField<void*>(context_, 0x298), 0x9c0) &= ~8u;
    }
    if (EngineField<std::uint8_t>(image, 0x4197134) == 1)
        EngineNative(image_base_, 0x21c8540, EngineField<void*>(context_, 0x298));
    EngineField<int>(engine_, 0x630) = 0;
    EngineNative(image_base_, 0x2170b20, engine_, context_);
    if (EngineField<std::uint8_t>(context_, 0) != 4
        && !EngineNative<bool>(image_base_, 0x1ef83d0, EngineField<void*>(context_, 0x298)))
        any_world_running_ = true;
}

void Sc6ReplayHost::ResetEngineWindow()
{
    auto* image = reinterpret_cast<void*>(image_base_);
    if (!EngineField<bool>(image, 0x4094270)) return;
    EngineField<bool>(image, 0x4094270) = false;
    EngineNative(image_base_, 0xe19ea0);
    auto* window = EngineField<void*>(engine_, 0xcd8);
    auto* control = EngineField<void*>(engine_, 0xce0);
    if (!window || !control || EngineField<int>(control, 8) <= 0) return;
    ++EngineField<int>(control, 8);
    EngineNative(image_base_, 0x104d860, window);
    if (--EngineField<int>(control, 8) == 0)
    {
        EngineVirtual(control, 0);
        if (--EngineField<int>(control, 0xc) == 0) EngineVirtual(control, 8, 1);
    }
    void* reference[2]{EngineField<void*>(engine_, 0xcf8), EngineField<void*>(engine_, 0xd00)};
    if (reference[1]) ++EngineField<int>(reference[1], 8);
    // 1410C84F0 consumes the argument's added reference on every return.
    EngineNative(image_base_, 0x10c84f0, EngineField<void*>(image, 0x42a8dd8), reference);
}

void* Sc6ReplayHost::RendererModule()
{
    auto& renderer = EngineField<void*>(reinterpret_cast<void*>(image_base_), 0x4394bb8);
    if (!renderer)
    {
        std::uint64_t name{};
        EngineNative<void*>(image_base_, 0xdf5d00, &name, L"Renderer", 1);
        renderer = EngineNative<void*>(image_base_, 0xdecf90,
            EngineNative<void*>(image_base_, 0xdea5f0), name, false);
    }
    return renderer;
}

void Sc6ReplayHost::DispatchEngineRenderClock()
{
    auto* image = reinterpret_cast<void*>(image_base_);
    if (!EngineField<bool>(image, 0x4351840) && (!EngineField<bool>(image, 0x419718c)
        || GetCurrentThreadId() == EngineField<std::uint32_t>(image, 0x419716c)))
    {
        const auto suspended = EngineField<bool>(image, 0x4351843);
        EngineNative(image_base_, 0x15edaf0);
        if (!suspended)
        {
            EngineField<float>(image, 0x4351b24) += engine_delta_;
            EngineField<float>(image, 0x4351b20) = engine_delta_;
        }
        EngineVirtual(RendererModule(), 0xa0);
        return;
    }
    std::uintptr_t storage[7]{};
    auto* builder = EngineNative<std::uintptr_t*>(image_base_, 0x1e2cc30, storage, 0, 0xff);
    auto* task = reinterpret_cast<void*>(builder[0]);
    EngineField<bool>(task, 0x10) = EngineField<bool>(image, 0x4351843);
    EngineField<float>(task, 0x14) = engine_delta_;
    auto* event = EngineField<void*>(task, 0x20);
    if (event) InterlockedIncrement(&EngineField<LONG>(event, 0x48));
    EngineNative(image_base_, 0x134c6c0, task, reinterpret_cast<void*>(builder[1]), static_cast<int>(builder[2]), true);
    if (event && InterlockedDecrement(&EngineField<LONG>(event, 0x48)) == 0)
        EngineNative(image_base_, 0xd2ebe0, event);
}

void Sc6ReplayHost::DrainEngine(float delta, bool idle_mode)
{
    if (!engine_idle()) __fastfail(FAST_FAIL_INVALID_ARG);
    engine_delta_ = delta;
    idle_mode_ = idle_mode;
    engine_phase_ = EnginePhase::Entry;
    while (!engine_idle())
    {
        AdvanceEngine();
        if (SuspendConsumerTask() || TrySuspendInterior()) return;
    }
}

void Sc6ReplayHost::AdvanceEngine()
{
    auto* image = reinterpret_cast<void*>(image_base_);
    switch (engine_phase_)
    {
    case EnginePhase::Entry: EnterEngine(); break;
    case EnginePhase::Context:
        if (context_index_ >= EngineField<int>(engine_, 0xbf0))
        { engine_phase_ = EnginePhase::TickUnbound; break; }
        context_ = EngineField<std::byte**>(engine_, 0xbe8)[context_index_];
        if (auto* world = EngineField<void*>(context_, 0x298);
            world && EngineField<bool>(world, 0x3b1))
        {
            EngineField<void*>(image, 0x43b4db8) = world;
            EngineVirtual(engine_, 0x400, context_, engine_delta_);
            engine_phase_ = EnginePhase::World;
        }
        else engine_phase_ = EnginePhase::NextContext;
        break;
    case EnginePhase::World:
        if (TickEngineWorld()) engine_phase_ = EnginePhase::WorldTail;
        break;
    case EnginePhase::WorldTail: TickEngineWorldTail(); engine_phase_ = EnginePhase::NextContext; break;
    case EnginePhase::NextContext: ++context_index_; engine_phase_ = EnginePhase::Context; break;
    case EnginePhase::TickUnbound:
        EngineNative(image_base_, 0x1f03080, static_cast<void*>(nullptr), 2, false, engine_delta_);
        if (saved_context_handle_)
            EngineField<void*>(image, 0x43b4db8) = EngineField<void*>(
                EngineNative<void*>(image_base_, 0x2178340, engine_, saved_context_handle_), 0x298);
        engine_phase_ = EnginePhase::Viewport; break;
    case EnginePhase::Viewport:
        if (auto* viewport = EngineField<void*>(engine_, 0x618); viewport && !idle_mode_)
            EngineVirtual(viewport, 0x280, engine_delta_);
        engine_phase_ = EnginePhase::ResetWindow; break;
    case EnginePhase::ResetWindow: ResetEngineWindow(); engine_phase_ = EnginePhase::Render; break;
    case EnginePhase::Render:
        if (!idle_mode_ && !EngineField<bool>(image, 0x418b12f))
        {
            EngineVirtual(engine_, 0x410, true);
            EngineVirtual(RendererModule(), 0x130);
        }
        engine_phase_ = EnginePhase::Device; break;
    case EnginePhase::Device:
        if (EngineField<bool>(image, 0x4197133))
            EngineVirtual(EngineNative<void*>(image_base_, 0x1dc4c70), 8, engine_delta_, false);
        engine_phase_ = EnginePhase::Audio; break;
    case EnginePhase::Audio:
        if (auto* audio = EngineNative<void*>(image_base_, 0x2177060, EngineField<void*>(image, 0x43b3068)))
            EngineNative(image_base_, 0x1d07440, audio, any_world_running_);
        engine_phase_ = EnginePhase::RenderClock; break;
    case EnginePhase::RenderClock: DispatchEngineRenderClock(); engine_phase_ = EnginePhase::Finish; break;
    case EnginePhase::Finish: ++completed_engines_; context_ = nullptr; engine_phase_ = EnginePhase::Idle; break;
    case EnginePhase::Idle: break;
    }
}
