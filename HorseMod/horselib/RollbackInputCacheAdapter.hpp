// ============================================================================
// Horse::RollbackInputCacheAdapter
//
// Pure cache-provenance model for the stock ALuxBattleFrameInputLog cache
// boundary. Runtime code must prove these ordering rules before any future live
// cache writer is enabled.
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse
{
    enum class RollbackInputCacheSource : uint8_t
    {
        Empty,
        StockDrain,
        Prediction,
        ConfirmedRemote,
    };

    enum class RollbackInputCacheAccessStatus : uint8_t
    {
        Ok,
        Duplicate,
        Conflict,
        InvalidSlot,
        InvalidSource,
        NetworkThreadCacheWrite,
        NotGameThread,
        StockDrainNotComplete,
        StockDrainAfterPrediction,
        PredictionOverConfirmed,
        CacheMiss,
        CacheIdentityMismatch,
        DecodedInputRouteMismatch,
    };

    struct FLuxReplayInputCacheEntry_Model
    {
        int32_t nFrameID {0};
        uint32_t dwFrameIndex {0};
        uint32_t dwInputValue {0};
        uint8_t bFilled {0};
        uint8_t abReserved[3] {};
    };

    static_assert(
        sizeof(FLuxReplayInputCacheEntry_Model) == 0x10,
        "SC6 replay input cache entry must stay 0x10 bytes");

    struct RollbackInputCacheWriteRequest
    {
        RollbackInputCacheSource source {RollbackInputCacheSource::Empty};
        uint32_t dwPlayerSlot {0};
        uint32_t dwFrameIndex {0};
        int32_t nLastFrameId {0};
        uint32_t dwInputValue {0};
        bool bOnGameThread {false};
        bool bNetworkThread {false};
        bool bStockDrainComplete {false};
        bool bDrainBypass {false};
    };

    struct RollbackInputCacheAccessReport
    {
        bool ok {false};
        bool wrote {false};
        bool duplicate {false};
        bool conflict {false};
        bool replaced_prediction {false};
        bool requires_correction {false};
        bool exactly_one_source {false};
        bool source_stock_drain {false};
        bool source_prediction {false};
        bool source_confirmed {false};
        RollbackInputCacheAccessStatus status {
            RollbackInputCacheAccessStatus::InvalidSource};
        uint32_t dwPlayerSlot {0};
        uint32_t dwFrameIndex {0};
        int32_t nLastFrameId {0};
        uint32_t dwInputValue {0};
        uint64_t qwSequence {0};
        const char* failure {"not-run"};
    };

    struct RollbackInputCacheAdapterSelfTestReport
    {
        bool ok {false};
        bool entry_layout_ok {false};
        bool prediction_after_drain {false};
        bool network_thread_rejected {false};
        bool not_game_thread_rejected {false};
        bool drain_order_required {false};
        bool stock_after_prediction_rejected {false};
        bool confirmed_replaces_prediction {false};
        bool duplicate_confirmed_idempotent {false};
        bool prediction_over_confirmed_rejected {false};
        bool conflict_rejected {false};
        bool consume_source_exact {false};
        bool ring_identity_mismatch_rejected {false};
        const char* failure {"not-run"};
    };

    static inline bool RollbackInputCacheSourceIsConfirmed(
        RollbackInputCacheSource source) noexcept
    {
        return source == RollbackInputCacheSource::StockDrain
            || source == RollbackInputCacheSource::ConfirmedRemote;
    }

    static inline void RollbackInputCacheMarkSource(
        RollbackInputCacheAccessReport& out,
        RollbackInputCacheSource source) noexcept
    {
        out.exactly_one_source = source != RollbackInputCacheSource::Empty;
        out.source_stock_drain =
            source == RollbackInputCacheSource::StockDrain;
        out.source_prediction =
            source == RollbackInputCacheSource::Prediction;
        out.source_confirmed =
            source == RollbackInputCacheSource::ConfirmedRemote
            || source == RollbackInputCacheSource::StockDrain;
    }

    template<size_t PlayerSlots, size_t FrameSlots>
    class RollbackInputCacheShadow
    {
    public:
        static_assert(PlayerSlots > 0, "player slot count must be non-zero");
        static_assert(FrameSlots > 0, "frame slot count must be non-zero");
        static_assert(
            (FrameSlots & (FrameSlots - 1)) == 0,
            "frame slot count must be a power of two");

        void clear() noexcept
        {
            m_cells = {};
            m_sequence = 0;
        }

        RollbackInputCacheAccessReport write(
            const RollbackInputCacheWriteRequest& req) noexcept
        {
            RollbackInputCacheAccessReport out {};
            out.failure = "ok";
            out.dwPlayerSlot = req.dwPlayerSlot;
            out.dwFrameIndex = req.dwFrameIndex;
            out.nLastFrameId = req.nLastFrameId;
            out.dwInputValue = req.dwInputValue;

            if (req.dwPlayerSlot >= PlayerSlots)
            {
                out.status = RollbackInputCacheAccessStatus::InvalidSlot;
                out.failure = "invalid-player-slot";
                return out;
            }
            if (req.source == RollbackInputCacheSource::Empty)
            {
                out.status = RollbackInputCacheAccessStatus::InvalidSource;
                out.failure = "invalid-cache-source";
                return out;
            }
            if (req.bNetworkThread)
            {
                out.status =
                    RollbackInputCacheAccessStatus::NetworkThreadCacheWrite;
                out.failure = "network-thread-live-cache-write-forbidden";
                return out;
            }
            if (!req.bOnGameThread)
            {
                out.status = RollbackInputCacheAccessStatus::NotGameThread;
                out.failure = "cache-write-not-game-thread";
                return out;
            }
            if (req.source == RollbackInputCacheSource::Prediction
                && !req.bDrainBypass && !req.bStockDrainComplete)
            {
                out.status =
                    RollbackInputCacheAccessStatus::StockDrainNotComplete;
                out.failure = "prediction-before-stock-drain";
                return out;
            }

            ShadowCell& cell = at(req.dwPlayerSlot, req.dwFrameIndex);
            const bool identity_matches =
                cell.entry.bFilled != 0
                && cell.entry.dwFrameIndex == req.dwFrameIndex
                && cell.entry.nFrameID == req.nLastFrameId;

            if (identity_matches)
            {
                if (cell.source == RollbackInputCacheSource::Prediction
                    && req.source == RollbackInputCacheSource::StockDrain)
                {
                    out.status =
                        RollbackInputCacheAccessStatus::StockDrainAfterPrediction;
                    out.failure = "stock-drain-after-prediction";
                    return out;
                }
                if (cell.source == RollbackInputCacheSource::Prediction
                    && req.source == RollbackInputCacheSource::ConfirmedRemote)
                {
                    out.replaced_prediction = true;
                    out.requires_correction =
                        cell.entry.dwInputValue != req.dwInputValue;
                }
                else if (RollbackInputCacheSourceIsConfirmed(cell.source)
                         && req.source == RollbackInputCacheSource::Prediction)
                {
                    out.status =
                        RollbackInputCacheAccessStatus::PredictionOverConfirmed;
                    out.failure = "prediction-over-confirmed-cache";
                    return out;
                }
                else if (cell.entry.dwInputValue == req.dwInputValue
                         && cell.source == req.source)
                {
                    out.ok = true;
                    out.duplicate = true;
                    out.status = RollbackInputCacheAccessStatus::Duplicate;
                    out.qwSequence = cell.qwSequence;
                    RollbackInputCacheMarkSource(out, cell.source);
                    return out;
                }
                else if (RollbackInputCacheSourceIsConfirmed(cell.source)
                         && RollbackInputCacheSourceIsConfirmed(req.source))
                {
                    out.conflict = true;
                    out.status = RollbackInputCacheAccessStatus::Conflict;
                    out.failure = "conflicting-confirmed-cache-input";
                    return out;
                }
                else if (cell.source == RollbackInputCacheSource::Prediction
                         && req.source == RollbackInputCacheSource::Prediction
                         && cell.entry.dwInputValue != req.dwInputValue)
                {
                    out.conflict = true;
                    out.status = RollbackInputCacheAccessStatus::Conflict;
                    out.failure = "conflicting-predicted-cache-input";
                    return out;
                }
            }

            cell.entry.nFrameID = req.nLastFrameId;
            cell.entry.dwFrameIndex = req.dwFrameIndex;
            cell.entry.dwInputValue = req.dwInputValue;
            cell.entry.bFilled = 1;
            cell.source = req.source;
            cell.qwSequence = ++m_sequence;

            out.ok = true;
            out.wrote = true;
            out.status = RollbackInputCacheAccessStatus::Ok;
            out.qwSequence = cell.qwSequence;
            RollbackInputCacheMarkSource(out, cell.source);
            return out;
        }

        RollbackInputCacheAccessReport consume(
            uint32_t dwPlayerSlot,
            uint32_t dwFrameIndex,
            int32_t nLastFrameId) const noexcept
        {
            RollbackInputCacheAccessReport out {};
            out.dwPlayerSlot = dwPlayerSlot;
            out.dwFrameIndex = dwFrameIndex;
            out.nLastFrameId = nLastFrameId;
            out.failure = "ok";

            if (dwPlayerSlot >= PlayerSlots)
            {
                out.status = RollbackInputCacheAccessStatus::InvalidSlot;
                out.failure = "invalid-player-slot";
                return out;
            }

            const ShadowCell& cell = at(dwPlayerSlot, dwFrameIndex);
            if (cell.entry.bFilled == 0)
            {
                out.status = RollbackInputCacheAccessStatus::CacheMiss;
                out.failure = "cache-miss";
                return out;
            }
            if (cell.entry.dwFrameIndex != dwFrameIndex
                || cell.entry.nFrameID != nLastFrameId)
            {
                out.status =
                    RollbackInputCacheAccessStatus::CacheIdentityMismatch;
                out.failure = "cache-identity-mismatch";
                return out;
            }

            out.ok = true;
            out.status = RollbackInputCacheAccessStatus::Ok;
            out.dwInputValue = cell.entry.dwInputValue;
            out.qwSequence = cell.qwSequence;
            RollbackInputCacheMarkSource(out, cell.source);
            return out;
        }

    private:
        struct ShadowCell
        {
            FLuxReplayInputCacheEntry_Model entry {};
            RollbackInputCacheSource source {RollbackInputCacheSource::Empty};
            uint64_t qwSequence {0};
        };

        static constexpr size_t index(
            uint32_t dwPlayerSlot,
            uint32_t dwFrameIndex) noexcept
        {
            return static_cast<size_t>(dwPlayerSlot) * FrameSlots
                + static_cast<size_t>(dwFrameIndex & (FrameSlots - 1));
        }

        ShadowCell& at(
            uint32_t dwPlayerSlot,
            uint32_t dwFrameIndex) noexcept
        {
            return m_cells[index(dwPlayerSlot, dwFrameIndex)];
        }

        const ShadowCell& at(
            uint32_t dwPlayerSlot,
            uint32_t dwFrameIndex) const noexcept
        {
            return m_cells[index(dwPlayerSlot, dwFrameIndex)];
        }

        std::array<ShadowCell, PlayerSlots * FrameSlots> m_cells {};
        uint64_t m_sequence {0};
    };

    static inline RollbackInputCacheWriteRequest
    RollbackInputCacheRequest(
        RollbackInputCacheSource source,
        uint32_t dwPlayerSlot,
        uint32_t dwFrameIndex,
        int32_t nLastFrameId,
        uint32_t dwInputValue) noexcept
    {
        RollbackInputCacheWriteRequest req {};
        req.source = source;
        req.dwPlayerSlot = dwPlayerSlot;
        req.dwFrameIndex = dwFrameIndex;
        req.nLastFrameId = nLastFrameId;
        req.dwInputValue = dwInputValue;
        req.bOnGameThread = true;
        return req;
    }

    static inline RollbackInputCacheAdapterSelfTestReport
    RunRollbackInputCacheAdapterSelfTest() noexcept
    {
        RollbackInputCacheAdapterSelfTestReport report {};
        report.failure = "ok";
        report.entry_layout_ok =
            sizeof(FLuxReplayInputCacheEntry_Model) == 0x10;

        RollbackInputCacheShadow<2, 512> cache {};
        cache.clear();

        RollbackInputCacheWriteRequest pred =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::Prediction, 1, 10, 10, 0x1010);
        pred.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport pred_write =
            cache.write(pred);
        const RollbackInputCacheAccessReport pred_consume =
            cache.consume(1, 10, 10);
        report.prediction_after_drain =
            pred_write.ok && pred_write.wrote
            && pred_consume.ok && pred_consume.source_prediction
            && pred_consume.exactly_one_source
            && pred_consume.dwInputValue == 0x1010;

        RollbackInputCacheWriteRequest network =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::Prediction, 1, 11, 11, 0x1111);
        network.bNetworkThread = true;
        network.bOnGameThread = false;
        network.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport network_write =
            cache.write(network);
        report.network_thread_rejected =
            !network_write.ok
            && network_write.status
                == RollbackInputCacheAccessStatus::NetworkThreadCacheWrite;

        RollbackInputCacheWriteRequest off_thread =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::Prediction, 1, 13, 13, 0x1313);
        off_thread.bOnGameThread = false;
        off_thread.bNetworkThread = false;
        off_thread.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport off_thread_write =
            cache.write(off_thread);
        report.not_game_thread_rejected =
            !off_thread_write.ok
            && off_thread_write.status
                == RollbackInputCacheAccessStatus::NotGameThread;

        RollbackInputCacheWriteRequest early =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::Prediction, 1, 12, 12, 0x1212);
        const RollbackInputCacheAccessReport early_write =
            cache.write(early);
        report.drain_order_required =
            !early_write.ok
            && early_write.status
                == RollbackInputCacheAccessStatus::StockDrainNotComplete;

        RollbackInputCacheWriteRequest stock_after_pred =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::StockDrain, 1, 10, 10, 0x1010);
        stock_after_pred.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport stock_after_pred_write =
            cache.write(stock_after_pred);
        report.stock_after_prediction_rejected =
            !stock_after_pred_write.ok
            && stock_after_pred_write.status
                == RollbackInputCacheAccessStatus::StockDrainAfterPrediction;

        RollbackInputCacheWriteRequest confirm =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::ConfirmedRemote,
                1, 10, 10, 0x2020);
        confirm.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport confirm_write =
            cache.write(confirm);
        const RollbackInputCacheAccessReport confirm_consume =
            cache.consume(1, 10, 10);
        report.confirmed_replaces_prediction =
            confirm_write.ok && confirm_write.wrote
            && confirm_write.replaced_prediction
            && confirm_write.requires_correction
            && confirm_consume.ok && confirm_consume.source_confirmed
            && confirm_consume.dwInputValue == 0x2020;

        const RollbackInputCacheAccessReport confirm_dup =
            cache.write(confirm);
        report.duplicate_confirmed_idempotent =
            confirm_dup.ok && confirm_dup.duplicate;

        RollbackInputCacheWriteRequest pred_over_confirmed =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::Prediction, 1, 10, 10, 0x4040);
        pred_over_confirmed.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport pred_over_confirmed_write =
            cache.write(pred_over_confirmed);
        const RollbackInputCacheAccessReport after_pred_over_confirmed =
            cache.consume(1, 10, 10);
        report.prediction_over_confirmed_rejected =
            !pred_over_confirmed_write.ok
            && pred_over_confirmed_write.status
                == RollbackInputCacheAccessStatus::PredictionOverConfirmed
            && after_pred_over_confirmed.ok
            && after_pred_over_confirmed.source_confirmed
            && after_pred_over_confirmed.dwInputValue == 0x2020;

        RollbackInputCacheWriteRequest conflict =
            RollbackInputCacheRequest(
                RollbackInputCacheSource::ConfirmedRemote,
                1, 10, 10, 0x3030);
        conflict.bStockDrainComplete = true;
        const RollbackInputCacheAccessReport conflict_write =
            cache.write(conflict);
        report.conflict_rejected =
            !conflict_write.ok
            && conflict_write.conflict
            && conflict_write.status
                == RollbackInputCacheAccessStatus::Conflict;

        report.consume_source_exact =
            confirm_consume.ok
            && confirm_consume.exactly_one_source
            && !confirm_consume.source_prediction
            && confirm_consume.source_confirmed;

        const RollbackInputCacheAccessReport ring_mismatch =
            cache.consume(1, 522, 10);
        report.ring_identity_mismatch_rejected =
            !ring_mismatch.ok
            && ring_mismatch.status
                == RollbackInputCacheAccessStatus::CacheIdentityMismatch;

        report.ok =
            report.entry_layout_ok
            && report.prediction_after_drain
            && report.network_thread_rejected
            && report.not_game_thread_rejected
            && report.drain_order_required
            && report.stock_after_prediction_rejected
            && report.confirmed_replaces_prediction
            && report.duplicate_confirmed_idempotent
            && report.prediction_over_confirmed_rejected
            && report.conflict_rejected
            && report.consume_source_exact
            && report.ring_identity_mismatch_rejected;
        if (!report.ok)
            report.failure = "input-cache-adapter-selftest-failed";
        return report;
    }
}
