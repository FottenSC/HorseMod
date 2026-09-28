    inline bool PresentHook::retain_replay_surface(ReplaySurfaceImage& output)
    {
        if (GetCurrentThreadId() != m_present_thread.load() || m_replay_drawing || !m_replay_armed
            || !m_replay_ready.load() || !m_replay_image || output.image || !m_replay_bytes.load()) return false;
        output.image = m_replay_image;
        output.image->GetDevice(output.device.GetAddressOf());
        output.bytes = m_replay_bytes.load();
        output.source_present = m_replay_source_present;
        return output.device != nullptr;
    }

    inline bool PresentHook::install_replay_surface(const ReplaySurfaceImage& target, ReplaySurfaceImage& undo, bool hold_native_display)
    {
        if (GetCurrentThreadId() != m_present_thread.load() || m_replay_drawing || !m_replay_armed
            || !m_replay_ready.load() || !m_replay_image || !m_game_swap_chain || !target.image
            || !target.device || undo.image || !target.bytes || target.bytes > m_replay_budget
            || (hold_native_display && m_replay_display_transaction)) return false;
        ComPtr<ID3D11Texture2D> backbuffer;
        if (FAILED(m_game_swap_chain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf())))) return false;
        D3D11_TEXTURE2D_DESC expected{}, actual{}, previous{};
        backbuffer->GetDesc(&expected);
        target.image->GetDesc(&actual);
        m_replay_image->GetDesc(&previous);
        ComPtr<ID3D11Device> device;
        backbuffer->GetDevice(device.GetAddressOf());
        if (target.device.Get() != device.Get() || device.Get() != DX11State::instance().device()
            || actual.Width != expected.Width || actual.Height != expected.Height || actual.Format != expected.Format
            || actual.SampleDesc.Count != 1 || actual.ArraySize != 1 || actual.MipLevels != 1
            || previous.Width != expected.Width || previous.Height != expected.Height || previous.Format != expected.Format
            || target.bytes != std::uint64_t{actual.Width} * actual.Height * 4) return false;
        // No GPU write or gameplay callback: the next held draw copies A and
        // redraws the current overlay. B remains retained for cancellation.
        // Equal images are valid for repeated zero-traversal landings. Retain
        // an independent undo reference after the same device/shape checks;
        // this changes no texture contents and performs no GPU submission.
        if (!retain_replay_surface(undo)) return false;
        m_replay_image = target.image;
        m_replay_source_present = target.source_present;
        m_replay_bytes.store(target.bytes);
        if(hold_native_display) {
            m_replay_display_hold_image=undo.image;
            m_replay_display_transaction=true;m_replay_display_published=false;
            m_replay_suppressed_presents.store(0);
        }
        return true;
    }

    inline bool PresentHook::undo_replay_surface(const ReplaySurfaceImage& target, ReplaySurfaceImage& undo)
    {
        if (GetCurrentThreadId() != m_present_thread.load() || m_replay_drawing || !m_replay_armed
            || !m_replay_ready.load() || !undo.image || m_replay_image.Get() != target.image.Get()) return false;
        const auto expected = undo.image.Get();
        ReplaySurfaceImage displaced;
        if (!install_replay_surface(undo, displaced)) return false;
        undo = {};
        m_replay_display_published=false;
        return m_replay_image.Get() == expected;
    }

