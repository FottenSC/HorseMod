#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;
#define STR(x) x
#define REQUIRE(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC {enum class LogLevel {Default};struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};}
struct DX11State {
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;HWND window{};
    static DX11State& instance(){static DX11State state;return state;}
    bool ready()const{return d && c;}auto device()const{return d.Get();}auto context()const{return c.Get();}auto hwnd()const{return window;}
};
struct PresentHook {
    std::atomic<DWORD> m_present_thread{GetCurrentThreadId()};
    ComPtr<IDXGISwapChain> m_game_swap_chain,m_replay_output_chain;
    ComPtr<ID3D11Texture2D> m_replay_image,m_replay_output_image,m_replay_backbuffer_undo,m_replay_native_target,m_replay_display_hold_image;
    ComPtr<ID3D11RenderTargetView> m_replay_output_rtv;
    ComPtr<ID3D11Query> m_replay_native_completion;
    ComPtr<ID3D11DeviceContext> m_replay_native_context;
    std::atomic<std::uint64_t> m_replay_scratch_bytes{},m_replay_transfer_bytes{};
    std::atomic<bool> m_replay_native_pending{};
    HWND m_replay_output_window{};bool m_replay_drawing{};
    bool m_replay_display_transaction{},m_replay_display_publishing{},m_replay_display_published{},draw_result=true;
    std::atomic<std::uint64_t> m_replay_suppressed_presents{},m_replay_bytes{};
    std::uint64_t m_replay_source_present{};unsigned draws{};
    struct ReplaySurfaceImage {ComPtr<ID3D11Texture2D> image;std::uint64_t bytes{},source_present{};};
    bool draw_replay_surface(){++draws;return draw_result;}
    bool publish_replay_surface(const ReplaySurfaceImage* expected=nullptr);
    bool finish_replay_display_transaction(bool);
    static inline unsigned presents{};
    static HRESULT Present(IDXGISwapChain*,UINT,UINT){++presents;return E_FAIL;}
    HRESULT(*m_original_present)(IDXGISwapChain*,UINT,UINT)=&Present;
    HRESULT present_replay_frame(IDXGISwapChain*,UINT,UINT);
    std::uint64_t replay_surface_bytes()const{return m_replay_scratch_bytes+m_replay_transfer_bytes;}
    bool prepare_replay_output(std::uint64_t);
    ID3D11Texture2D* replay_draw_image() const noexcept;
    bool stage_replay_native_draw();bool restore_replay_native_backbuffer();bool release_replay_output();bool retire_replay_surface();
};
#include "replay_native_viewport_methods.inl"
static void Complete(PresentHook& owner) {
    auto& state=DX11State::instance();
    state.c->End(owner.m_replay_native_completion.Get());state.c->Flush();
    const auto until=GetTickCount64()+5000;
    while(!owner.retire_replay_surface()){REQUIRE(GetTickCount64()<until);SwitchToThread();}
}
static void Pixels(ID3D11Texture2D* texture,std::uint32_t value) {
    auto& state=DX11State::instance();D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> read;REQUIRE(SUCCEEDED(state.d->CreateTexture2D(&desc,nullptr,read.GetAddressOf())));
    state.c->CopyResource(read.Get(),texture);D3D11_MAPPED_SUBRESOURCE map{};
    REQUIRE(SUCCEEDED(state.c->Map(read.Get(),0,D3D11_MAP_READ,0,&map)));
    for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)
        REQUIRE(reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(map.pData)+y*map.RowPitch)[x]==value);
    state.c->Unmap(read.Get(),0);
}
int main() {
    auto& state=DX11State::instance();
    // Hidden fixture window only; production methods must neither create nor
    // destroy it. WARP exercises real resource copies and GPU event completion.
    state.window=CreateWindowExW(0,L"STATIC",L"viewport ownership fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    REQUIRE(state.window);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=desc.BufferDesc.Height=32;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=state.window;
    desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    PresentHook owner;
    REQUIRE(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &desc,owner.m_game_swap_chain.GetAddressOf(),state.d.GetAddressOf(),nullptr,state.c.GetAddressOf())));
    REQUIRE(!owner.prepare_replay_output(1) && !owner.m_replay_output_chain && !owner.replay_surface_bytes());
    REQUIRE(owner.prepare_replay_output(1024*1024));
    REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),1,0)==E_FAIL && owner.presents==1);
    owner.m_replay_display_transaction=true;
    REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),1,0)==S_OK && owner.presents==1);
    owner.m_replay_drawing=true;
    REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),1,0)==S_OK && owner.presents==1);
    owner.m_replay_display_publishing=true;
    REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),1,0)==E_FAIL && owner.presents==2);
    owner.m_replay_drawing=owner.m_replay_display_publishing=false;
    REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),1,DXGI_PRESENT_TEST)==E_FAIL && owner.presents==3);
    REQUIRE(owner.present_replay_frame(nullptr,1,0)==E_FAIL && owner.presents==4);
    REQUIRE(owner.m_replay_suppressed_presents==2);
    owner.m_replay_display_transaction=false;
    REQUIRE(owner.m_replay_output_chain.Get()==owner.m_game_swap_chain.Get() && owner.m_replay_output_window==state.window);
    REQUIRE(owner.prepare_replay_output(1024*1024));
    D3D11_TEXTURE2D_DESC image{};owner.m_replay_output_image->GetDesc(&image);
    image.Usage=D3D11_USAGE_DEFAULT;image.BindFlags=image.CPUAccessFlags=image.MiscFlags=0;
    REQUIRE(SUCCEEDED(state.d->CreateTexture2D(&image,nullptr,owner.m_replay_image.GetAddressOf())));
    std::vector<std::uint32_t> native(32*32,0xff392817),held(32*32,0xffacbdce);
    for(unsigned attempt=0;attempt<2;++attempt) {
        state.c->UpdateSubresource(owner.m_replay_output_image.Get(),0,nullptr,native.data(),32*4,0);
        state.c->UpdateSubresource(owner.m_replay_image.Get(),0,nullptr,held.data(),32*4,0);
        REQUIRE(!owner.stage_replay_native_draw()); // Requires owned in-flight lifetime.
        owner.m_replay_native_context=state.c;owner.m_replay_native_target=owner.m_replay_image;owner.m_replay_native_pending=true;
        REQUIRE(owner.stage_replay_native_draw());Pixels(owner.m_replay_output_image.Get(),held[0]);
        const auto result=attempt?E_FAIL:owner.m_game_swap_chain->Present(0,0);
        REQUIRE(attempt || SUCCEEDED(result));
        REQUIRE(owner.restore_replay_native_backbuffer());
        REQUIRE(!owner.release_replay_output() && owner.m_replay_backbuffer_undo);
        Complete(owner);Pixels(owner.m_replay_output_image.Get(),native[0]);Pixels(owner.m_replay_image.Get(),held[0]);
    }
    {
        REQUIRE(SUCCEEDED(state.d->CreateTexture2D(&image,nullptr,owner.m_replay_display_hold_image.GetAddressOf())));
        const std::vector<std::uint32_t> b(32*32,0xff112233);
        state.c->UpdateSubresource(owner.m_replay_display_hold_image.Get(),0,nullptr,b.data(),32*4,0);
        owner.m_replay_display_transaction=true;
        owner.m_replay_native_context=state.c;owner.m_replay_native_target=owner.m_replay_display_hold_image;owner.m_replay_native_pending=true;
        REQUIRE(owner.replay_draw_image()==owner.m_replay_display_hold_image.Get());
        owner.m_replay_display_publishing=true;REQUIRE(!owner.stage_replay_native_draw());owner.m_replay_display_publishing=false;
        REQUIRE(owner.stage_replay_native_draw());Pixels(owner.m_replay_output_image.Get(),b[0]);
        const auto presents=owner.presents;
        owner.m_replay_drawing=true;REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),0,0)==E_FAIL&&owner.presents==presents+1);
        owner.m_replay_drawing=false;REQUIRE(owner.present_replay_frame(owner.m_game_swap_chain.Get(),0,0)==S_OK&&owner.presents==presents+1);
        REQUIRE(owner.restore_replay_native_backbuffer());Complete(owner);
        Pixels(owner.m_replay_output_image.Get(),native[0]);Pixels(owner.m_replay_image.Get(),held[0]);
        owner.m_replay_display_transaction=false;owner.m_replay_display_hold_image.Reset();
    }
    const auto bytes=owner.replay_surface_bytes();REQUIRE(bytes==32*32*4+4096);
    REQUIRE(!owner.publish_replay_surface() && !owner.draws);
    owner.m_replay_display_transaction=true;
    REQUIRE(!owner.finish_replay_display_transaction(true) && !owner.draws);
    owner.draw_result=false;
    REQUIRE(!owner.publish_replay_surface() && owner.m_replay_display_transaction && !owner.m_replay_display_published);
    owner.draw_result=true;
    REQUIRE(owner.publish_replay_surface() && owner.m_replay_display_transaction && owner.m_replay_display_published && owner.m_replay_display_hold_image==owner.m_replay_image);
    REQUIRE(!owner.publish_replay_surface());
    REQUIRE(owner.finish_replay_display_transaction(true) && !owner.m_replay_display_transaction && !owner.m_replay_display_hold_image);
    owner.m_replay_display_transaction=true;
    REQUIRE(owner.finish_replay_display_transaction(false) && !owner.m_replay_display_transaction);
    REQUIRE(owner.release_replay_output() && owner.release_replay_output() && !owner.replay_surface_bytes());
    REQUIRE(IsWindow(state.window)); // Cleanup must never close the game window.
    owner.m_game_swap_chain.Reset();DestroyWindow(state.window);
    std::puts("Original DXGI viewport identity, WARP copy/restore, failed Present cleanup and completed retirement passed");
}
