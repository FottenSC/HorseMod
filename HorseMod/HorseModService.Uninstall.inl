    HORSE_MOD_API void uninstall_mod(CppUserModBase* mod)
    {
        if(mod && !static_cast<HorseMod*>(mod)->PrepareForDestruction()) {
            // The void UE4SS callback cannot acknowledge pending shutdown.
            // Preserve both the entire object and its module pin. A later
            // uninstall request may retry after native owners have retired.
            if(!g_horse_mod_unload_guard_ready.load(std::memory_order_acquire)
                || !g_horse_mod_deferred_unload_pin.load(std::memory_order_acquire))
                __fastfail(FAST_FAIL_INVALID_ARG);
            RC::Output::send<RC::LogLevel::Warning>(STR("[HorseMod] uninstall deferred native_owners_pending=true object_retained=true module_pinned=true\n"));
            return;
        }
        auto* expected = static_cast<HorseMod*>(mod);
        (void)g_horse_mod_instance.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel);
        delete mod;
        g_horse_mod_unload_guard_ready.store(false,
            std::memory_order_release);
        if (auto pin = g_horse_mod_deferred_unload_pin.exchange(
                nullptr, std::memory_order_acq_rel))
            FreeLibrary(pin);
    }
