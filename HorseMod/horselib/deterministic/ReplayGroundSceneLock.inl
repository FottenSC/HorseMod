// Shared native scene lock used by production admission and shipped-SDK fixtures.
    struct SceneLock {
        std::uintptr_t scene{},module{};bool held{};
        bool Enter() noexcept {
            __try {
                const auto* table=*reinterpret_cast<const std::uintptr_t* const*>(scene);
                if(table[0x330/8]!=module+0x431d0 || table[0x338/8]!=module+0x432b0)return false;
                reinterpret_cast<void(*)(void*,const char*,unsigned)>(module+0x431d0)(reinterpret_cast<void*>(scene),__FILE__,__LINE__);
                held=true;return true;
            } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        }
        ~SceneLock() {if(held)reinterpret_cast<void(*)(void*)>(module+0x432b0)(reinterpret_cast<void*>(scene));}
    };
