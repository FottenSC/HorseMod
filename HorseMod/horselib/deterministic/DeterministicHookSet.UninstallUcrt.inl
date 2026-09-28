bool DeterministicHookSet::UninstallUcrtIatHooks() noexcept
{
    const auto restore = [](std::uintptr_t& slot, void* hook, void* original,std::uint32_t& protection) {
        if(slot==0)return protection==0;
        if(!original)return false;
        __try {
            if(!protection) {
                DWORD old{};
                if(!::VirtualProtect(reinterpret_cast<void*>(slot),sizeof(void*),PAGE_READWRITE,&old))return false;
                protection=old;
            }
            const auto previous=::InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot),original,hook);
            DWORD ignored{};
            if(!::VirtualProtect(reinterpret_cast<void*>(slot),sizeof(void*),protection,&ignored))return false;
            protection=0;
            // A foreign hook can still reference our trampoline. Do not erase
            // its lease or permit module deletion merely because CAS rejected.
            if(previous!=hook && previous!=original)return false;
            slot=0;return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    };
    return restore(srand_iat_slot_,reinterpret_cast<void*>(&UcrtSrandDetour),reinterpret_cast<void*>(original_srand_),iat_retirement_protection_[0])
        && restore(rand_iat_slot_,reinterpret_cast<void*>(&UcrtRandDetour),reinterpret_cast<void*>(original_rand_),iat_retirement_protection_[1]);
}
