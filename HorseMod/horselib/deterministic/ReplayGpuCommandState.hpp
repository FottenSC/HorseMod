#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace Horse::Deterministic {
// Checkpoint transfers must execute even when the renderer's occlusion
// predicate suppresses a draw. Restore the exact borrowed context state;
// this scope supplies neither submission ordering nor GPU completion.
class ReplayGpuUnconditionalCommands final {
public:
    explicit ReplayGpuUnconditionalCommands(ID3D11DeviceContext* context) noexcept : context_(context) {
        context_->GetPredication(&predicate_,&value_);
        context_->SetPredication(nullptr,FALSE);
    }
    ~ReplayGpuUnconditionalCommands() {context_->SetPredication(predicate_.Get(),value_);}
    ReplayGpuUnconditionalCommands(const ReplayGpuUnconditionalCommands&)=delete;
    ReplayGpuUnconditionalCommands& operator=(const ReplayGpuUnconditionalCommands&)=delete;
private:
    ID3D11DeviceContext* context_;
    Microsoft::WRL::ComPtr<ID3D11Predicate> predicate_;
    BOOL value_{};
};
// Separate scope permits callers with native SEH guards to use the same
// unconditional transfer without introducing C++ unwinding into __try.
inline void ReplayGpuCopyResource(ID3D11DeviceContext* context,ID3D11Resource* destination,ID3D11Resource* source) noexcept {
    ReplayGpuUnconditionalCommands commands(context);
    context->CopyResource(destination,source);
}
}
