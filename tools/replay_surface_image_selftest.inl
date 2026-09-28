// Exercise the actual production COM-image methods with real WARP resources.
// The backbuffer query is controlled; no window, rendering or GPU readback.
namespace SurfaceImageTransactionTest {
using Microsoft::WRL::ComPtr;
struct DX11State {
    ComPtr<ID3D11Device> value;
    static DX11State& instance(){static DX11State state;return state;}
    ID3D11Device* device(){return value.Get();}
};
struct Backbuffer {
    ComPtr<ID3D11Texture2D> image;
    HRESULT GetBuffer(unsigned index,REFIID iid,void** output) {
        return index?E_INVALIDARG:image->QueryInterface(iid,output);
    }
};
struct PresentHook {
    struct ReplaySurfaceImage {
        ComPtr<ID3D11Texture2D> image;
        ComPtr<ID3D11Device> device;
        std::uint64_t bytes{},source_present{};
    };
    std::atomic<DWORD> m_present_thread{GetCurrentThreadId()};
    bool m_replay_drawing{},m_replay_armed=true;
    bool m_replay_display_transaction{},m_replay_display_published{};
    std::atomic<std::uint64_t> m_replay_suppressed_presents{};
    std::atomic<bool> m_replay_ready{true};
    ComPtr<ID3D11Texture2D> m_replay_image,m_replay_display_hold_image;
    Backbuffer* m_game_swap_chain{};
    std::uint64_t m_replay_budget=64,m_replay_source_present{};
    std::atomic<std::uint64_t> m_replay_bytes{64};
    bool retain_replay_surface(ReplaySurfaceImage&);
    bool install_replay_surface(const ReplaySurfaceImage&,ReplaySurfaceImage&,bool=false);
    bool undo_replay_surface(const ReplaySurfaceImage&,ReplaySurfaceImage&);
};
#include "GameImGui/ReplaySurfaceTransaction.inl"
void Run() {
    auto& device=DX11State::instance().value;
    ComPtr<ID3D11DeviceContext> context;
    expect(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,device.GetAddressOf(),nullptr,context.GetAddressOf())),"surface transaction WARP device");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=4;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
    Backbuffer back;expect(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,back.image.GetAddressOf())),"surface backbuffer");
    for(bool same:{false,true}) {
        PresentHook host;host.m_game_swap_chain=&back;host.m_replay_image=back.image;
        PresentHook::ReplaySurfaceImage a,b;
        a.device=device;a.bytes=64;a.source_present=7;
        if(same)a.image=back.image;
        else expect(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,a.image.GetAddressOf())),"distinct A display image");
        host.m_present_thread=0;
        expect(!host.install_replay_surface(a,b) && !b.image,"wrong render owner rejects same and distinct images");
        host.m_present_thread=GetCurrentThreadId();a.bytes=63;
        expect(!host.install_replay_surface(a,b) && !b.image,"invalid extent rejects before retaining undo");
        a.bytes=64;host.m_replay_drawing=true;
        expect(!host.install_replay_surface(a,b) && !b.image,"in-flight draw rejects publication");
        host.m_replay_drawing=false;
        expect(host.install_replay_surface(a,b,true) && b.image==back.image && host.m_replay_image==a.image
            && host.m_replay_display_hold_image==b.image,
            "same or distinct A publication retains independent B reference");
        expect(!host.install_replay_surface(a,b) && b.image==back.image,"populated undo cannot be overwritten");
        ComPtr<ID3D11Texture2D> foreign;
        expect(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,foreign.GetAddressOf())),"foreign display image");
        host.m_replay_image=foreign;
        expect(!host.undo_replay_surface(a,b) && b.image==back.image,"foreign publication preserves B on rejection");
        host.m_replay_image=a.image;
        expect(host.undo_replay_surface(a,b) && !b.image && host.m_replay_image==back.image,
            "same or distinct publication recovers exactly the original B reference");
        expect(!host.undo_replay_surface(a,b),"surface undo is exactly once");
    }
    context.Reset();device.Reset();
}
}
