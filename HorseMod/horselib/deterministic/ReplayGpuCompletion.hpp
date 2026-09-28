#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <atomic>
#include <cstdint>

namespace Horse::Deterministic
{
// One transfer's completion contract. The enclosing transaction owns the
// source/destination leases until retired(), including after timeout/cancel.
// All methods run under the host's admitted immediate-context ownership.
// This object does not establish native command-list or producer ordering.
class ReplayGpuCompletion final
{
public:
    ReplayGpuCompletion() = default;
    ReplayGpuCompletion(const ReplayGpuCompletion&) = delete;
    ReplayGpuCompletion& operator=(const ReplayGpuCompletion&) = delete;
    using Clock = std::chrono::steady_clock;
    enum class Result { Empty, Prepared, Pending, Complete, TimedOut, Cancelled, DeviceFailure, Error };

    HRESULT Prepare(ID3D11Device* device) noexcept
    {
        if (!device || in_flight_ || query_) return E_INVALIDARG;
        D3D11_QUERY_DESC descriptor{D3D11_QUERY_EVENT, 0};
        const auto hr = device->CreateQuery(&descriptor, query_.GetAddressOf());
        if (FAILED(hr)) { result_ = Result::Error; return error_ = hr; }
        device_ = device;
        result_ = Result::Prepared;
        return S_OK;
    }

    HRESULT Submit(ID3D11DeviceContext* context, Clock::time_point deadline) noexcept
    {
        if (result_ != Result::Prepared || !ValidContext(context) || submitted_serial_.load() == UINT64_MAX) return E_INVALIDARG;
        context_ = context;
        deadline_ = deadline;
        in_flight_ = true;
        result_ = Result::Pending;
        // Caller has already enqueued all transfer commands. Explicit Flush
        // supplies progress while normal frame submission is suspended.
        submitted_serial_.fetch_add(1);
        context_->End(query_.Get());
        context_->Flush();
        return S_OK;
    }

    Result Poll(ID3D11DeviceContext* context, Clock::time_point now) noexcept
    {
        if (!in_flight_) return result_;
        if (context != context_.Get()) { error_ = E_INVALIDARG; return Result::Error; }
        // Deadline belongs to the whole request, not a busy-wait sub-loop.
        // Retirement continues after expiration, but expiration stays failure.
        if (result_ == Result::Pending && now >= deadline_) result_ = Result::TimedOut;
        BOOL signaled = FALSE;
        // Native Present is suspended during this transaction. Permit the
        // driver to flush pending query work; DONOTFLUSH polling alone can
        // prevent progress even though CPU submission has already returned.
        const auto hr = context_->GetData(query_.Get(), &signaled, sizeof(signaled), 0);
        if (hr == S_OK && signaled)
        {
            in_flight_ = false;
            if (result_ == Result::Pending) result_ = Result::Complete;
        }
        else if (FAILED(hr))
        {
            error_ = hr;
            const auto removed = device_->GetDeviceRemovedReason();
            if (FAILED(removed))
            {
                error_ = removed;
                in_flight_ = false; // Device loss, not successful GPU completion.
                result_ = Result::DeviceFailure;
            }
            else result_ = Result::Error; // Retain leases: failure did not retire the work.
        }
        return result_;
    }

    void Cancel() noexcept
    {
        if (result_ == Result::Prepared || result_ == Result::Pending) result_ = Result::Cancelled;
    }

    bool Release() noexcept
    {
        if (in_flight_) return false;
        context_.Reset(); query_.Reset(); device_.Reset();
        result_ = Result::Empty; error_ = S_OK;
        return true;
    }

    // Monotonic for this retained completion owner, including query reuse and failures.
    std::uint64_t submitted_serial() const noexcept { return submitted_serial_.load(); }
    bool retired() const noexcept { return !in_flight_; }
    Result result() const noexcept { return result_; }
    HRESULT error() const noexcept { return error_; }

private:
    bool ValidContext(ID3D11DeviceContext* context) const noexcept
    {
        if (!context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context->GetDevice(device.GetAddressOf());
        return device.Get() == device_.Get();
    }
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Query> query_;
    Clock::time_point deadline_{};
    Result result_{Result::Empty};
    HRESULT error_{S_OK};
    bool in_flight_{};
    std::atomic<std::uint64_t> submitted_serial_{};
};
}
