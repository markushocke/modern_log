module;

#include <cstdint>

export module modern.log.metrics;

export namespace modern::log {

struct async_metrics final {
	std::uint64_t enqueue_attempts{};
	std::uint64_t enqueued_records{};
	std::uint64_t dropped_records{};
	std::uint64_t enqueue_failures{};
	std::uint64_t current_queue_depth{};
	std::uint64_t high_water_mark{};
	std::uint64_t written_records{};
	std::uint64_t sink_write_calls{};
	std::uint64_t sink_flush_calls{};
	std::uint64_t flush_count{};
	std::uint64_t write_failures{};
};

} // namespace modern::log