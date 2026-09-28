    // Render-thread ownership for the original game swap chain. No HWND or
    // swap chain is created here. DISCARD requires explicit native-content
    // restoration after Present, even when the displayed image is unchanged.
    inline HRESULT PresentHook::present_replay_frame(IDXGISwapChain* swap_chain,UINT sync_interval,UINT flags)
    {
        if(m_replay_display_transaction && !(m_replay_drawing && (m_replay_display_publishing
                || (m_replay_display_hold_image && m_replay_native_target.Get()==m_replay_display_hold_image.Get())))
            && swap_chain==m_game_swap_chain.Get() && !(flags&DXGI_PRESENT_TEST)) {
            m_replay_suppressed_presents.fetch_add(1);
            return S_OK;
        }
        return m_original_present(swap_chain,sync_interval,flags);
    }

    inline bool PresentHook::prepare_replay_output(std::uint64_t budget_bytes)
    {
        auto& state=DX11State::instance();
        if(GetCurrentThreadId()!=m_present_thread.load() || !state.ready()
            || !m_game_swap_chain || m_replay_native_pending) return false;
        DXGI_SWAP_CHAIN_DESC native{};
        if(FAILED(m_game_swap_chain->GetDesc(&native)) || !native.Windowed
            || native.OutputWindow!=state.hwnd() || native.SwapEffect!=DXGI_SWAP_EFFECT_DISCARD) return false;
        ComPtr<ID3D11Texture2D> source;
        ComPtr<ID3D11Device> device;
        if(FAILED(m_game_swap_chain->GetBuffer(0,IID_PPV_ARGS(source.GetAddressOf())))) return false;
        source->GetDevice(device.GetAddressOf());
        D3D11_TEXTURE2D_DESC image{};source->GetDesc(&image);
        if(device.Get()!=state.device() || image.SampleDesc.Count!=1 || image.ArraySize!=1 || image.MipLevels!=1
            || !image.Width || !image.Height
            || (image.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && image.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                && image.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && image.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                && image.Format!=DXGI_FORMAT_R10G10B10A2_UNORM)) return false;
        if(m_replay_output_chain) {
            return m_replay_output_chain.Get()==m_game_swap_chain.Get()
                && m_replay_output_image.Get()==source.Get() && m_replay_backbuffer_undo
                && m_replay_output_rtv && m_replay_native_completion
                && m_replay_output_window==native.OutputWindow && IsWindow(native.OutputWindow)
                && replay_surface_bytes()<=budget_bytes;
        }
        const auto image_bytes=std::uint64_t{image.Width}*image.Height*4;
        // One owned scratch image and bounded RTV/query metadata. The original
        // swap chain/backbuffer are borrowed; no duplicate native output exists.
        const auto bytes=image_bytes+4096;
        const auto existing=replay_surface_bytes();
        const auto capture_reserve=m_replay_image?0:image_bytes;
        if(existing>budget_bytes || bytes>budget_bytes-existing || capture_reserve>budget_bytes-existing-bytes) return false;
        ComPtr<ID3D11Texture2D> backup;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11Query> completion;
        image.Usage=D3D11_USAGE_DEFAULT;
        image.BindFlags=image.CPUAccessFlags=image.MiscFlags=0;
        const D3D11_QUERY_DESC event{D3D11_QUERY_EVENT,0};
        if(FAILED(state.device()->CreateTexture2D(&image,nullptr,backup.GetAddressOf()))
            || FAILED(state.device()->CreateRenderTargetView(source.Get(),nullptr,rtv.GetAddressOf()))
            || FAILED(state.device()->CreateQuery(&event,completion.GetAddressOf()))) return false;
        m_replay_output_window=native.OutputWindow;
        m_replay_output_chain=m_game_swap_chain;
        m_replay_output_image=std::move(source);
        m_replay_output_rtv=std::move(rtv);
        m_replay_backbuffer_undo=std::move(backup);
        m_replay_native_completion=std::move(completion);
        m_replay_scratch_bytes.store(bytes);
        RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] native replay viewport prepared hwnd={} bytes={} swap_effect={} buffers={}\n"),
            reinterpret_cast<std::uintptr_t>(native.OutputWindow),bytes,static_cast<unsigned>(native.SwapEffect),native.BufferCount);
        return true;
    }

    inline ID3D11Texture2D* PresentHook::replay_draw_image() const noexcept
    {
        // B remains the display source while A and unsettled C are installed.
        // Successful explicit publication advances this held source to C.
        return m_replay_display_transaction && !m_replay_display_publishing
            ?m_replay_display_hold_image.Get():m_replay_image.Get();
    }

    inline bool PresentHook::stage_replay_native_draw()
    {
        auto& state=DX11State::instance();
        if(GetCurrentThreadId()!=m_present_thread.load() || !m_replay_native_pending
            || !m_replay_native_target || m_replay_native_target.Get()!=replay_draw_image() || !m_replay_backbuffer_undo || !m_replay_output_image
            || m_replay_output_chain.Get()!=m_game_swap_chain.Get()) return false;
        ComPtr<ID3D11Texture2D> current;
        if(FAILED(m_game_swap_chain->GetBuffer(0,IID_PPV_ARGS(current.GetAddressOf())))
            || current.Get()!=m_replay_output_image.Get() || current.Get()==m_replay_native_target.Get()) return false;
        D3D11_TEXTURE2D_DESC source{},target{};
        m_replay_native_target->GetDesc(&source);current->GetDesc(&target);
        ComPtr<ID3D11Device> device;m_replay_native_target->GetDevice(device.GetAddressOf());
        if(device.Get()!=state.device() || source.Width!=target.Width || source.Height!=target.Height
            || source.Format!=target.Format || source.SampleDesc.Count!=1 || source.ArraySize!=1 || source.MipLevels!=1) return false;
        state.context()->CopyResource(m_replay_backbuffer_undo.Get(),current.Get());
        state.context()->CopyResource(current.Get(),m_replay_native_target.Get());
        return true;
    }

    inline bool PresentHook::restore_replay_native_backbuffer()
    {
        if(GetCurrentThreadId()!=m_present_thread.load() || !m_replay_native_pending
            || !m_replay_backbuffer_undo || !m_replay_output_image || !m_game_swap_chain) return false;
        // Restore the exact retained native resource even if a callback has
        // invalidated the swap-chain identity. Such a change still rejects
        // continuation; the enclosing GPU event retains both copy operands.
        DX11State::instance().context()->CopyResource(m_replay_output_image.Get(),m_replay_backbuffer_undo.Get());
        ComPtr<ID3D11Texture2D> current;
        return SUCCEEDED(m_game_swap_chain->GetBuffer(0,IID_PPV_ARGS(current.GetAddressOf())))
            && current.Get()==m_replay_output_image.Get();
    }

    inline bool PresentHook::release_replay_output()
    {
        if(GetCurrentThreadId()!=m_present_thread.load() || m_replay_native_pending || m_replay_drawing) return false;
        if(m_replay_output_window) RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] native replay viewport retired hwnd={} gpu_complete=true\n"),
            reinterpret_cast<std::uintptr_t>(m_replay_output_window));
        m_replay_output_rtv.Reset();m_replay_output_image.Reset();m_replay_output_chain.Reset();
        m_replay_backbuffer_undo.Reset();m_replay_output_window=nullptr;
        m_replay_scratch_bytes.store(0);
        return true;
    }

    inline bool PresentHook::publish_replay_surface(const ReplaySurfaceImage* expected)
    {
        if(GetCurrentThreadId()!=m_present_thread.load() || !m_replay_display_transaction
            || m_replay_display_publishing || m_replay_display_published || m_replay_native_pending) return false;
        if(expected && (!expected->image || expected->image.Get()!=m_replay_image.Get()
            || expected->source_present!=m_replay_source_present || expected->bytes!=m_replay_bytes.load())) return false;
        m_replay_display_publishing=true;
        const bool complete=draw_replay_surface();
        m_replay_display_publishing=false;
        m_replay_display_published=complete;
        if(complete)m_replay_display_hold_image=m_replay_image;
        return complete;
    }

    inline bool PresentHook::finish_replay_display_transaction(bool commit)
    {
        if(GetCurrentThreadId()!=m_present_thread.load() || m_replay_native_pending || m_replay_drawing
            || !m_replay_display_transaction || (commit && !m_replay_display_published)) return false;
        if(!commit && !publish_replay_surface()) return false;
        RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] native replay viewport transaction finished commit={} suppressed_presents={} gpu_complete=true\n"),
            commit,m_replay_suppressed_presents.load());
        m_replay_display_transaction=false;m_replay_display_published=false;
        m_replay_display_hold_image.Reset();
        return true;
    }
