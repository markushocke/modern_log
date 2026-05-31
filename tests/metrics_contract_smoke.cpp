import modern.log.metrics;

#include <type_traits>

int main() {
	using modern::log::async_metrics;

	static_assert(std::is_standard_layout_v<async_metrics>);

	async_metrics metrics{};
	metrics.enqueue_attempts = 4;
	metrics.enqueued_records = 3;
	metrics.current_queue_depth = 2;
	metrics.sink_write_calls = 3;
	metrics.sink_flush_calls = 2;

	return metrics.enqueue_attempts == 4
		&& metrics.enqueued_records == 3
		&& metrics.current_queue_depth == 2
		&& metrics.sink_write_calls == 3
		&& metrics.sink_flush_calls == 2
		? 0
		: 1;
}