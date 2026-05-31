#pragma once

#include <cstddef>
#include <cstdint>

namespace modern::log::detail {

enum class backpressure_policy : std::uint8_t {
    drop_oldest,
    drop_newest,
    block_producer,
    sample_debug_logs,
    priority_preserve_errors,
};

enum class flush_trigger : std::uint8_t {
    explicit_request,
    periodic_interval,
    batch_threshold,
    shutdown_drain,
};

struct queue_contract final {
    std::size_t capacity_records{4096};
    std::size_t batch_record_limit{256};
    std::size_t wakeup_record_threshold{64};
    bool fifo_ordering{true};
    bool single_consumer{true};
};

struct lifecycle_contract final {
    bool explicit_flush_drains_pending{true};
    bool shutdown_drains_pending{true};
    bool destructor_calls_shutdown{true};
    bool sink_failures_increment_counters{true};
};

[[nodiscard]] constexpr bool valid(const queue_contract& contract) noexcept {
    return contract.capacity_records > 0
        && contract.batch_record_limit > 0
        && contract.batch_record_limit <= contract.capacity_records
        && contract.wakeup_record_threshold > 0
        && contract.wakeup_record_threshold <= contract.batch_record_limit
        && contract.fifo_ordering
        && contract.single_consumer;
}

} // namespace modern::log::detail