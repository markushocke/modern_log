import modern.log;

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using steady_clock = std::chrono::steady_clock;
using nanoseconds = std::chrono::nanoseconds;

struct benchmark_options final {
	std::size_t records{200000};
};

struct formatter_result final {
	std::string_view name;
	std::size_t records{};
	std::uint64_t elapsed_ns{};
	std::size_t emitted_bytes{};
};

struct async_config final {
	std::size_t queue_capacity{};
	std::size_t batch_limit{};
	std::size_t wakeup_threshold{};
	std::chrono::milliseconds flush_interval{};
};

struct async_result final {
	std::string_view name;
	std::size_t records{};
	std::uint64_t elapsed_ns{};
	std::uint64_t enqueue_p50_ns{};
	std::uint64_t enqueue_p95_ns{};
	std::uint64_t sink_write_calls{};
	std::uint64_t sink_flush_calls{};
	std::uint64_t high_water_mark{};
};

class counting_sink final : public modern::log::sink {
public:
	void write(modern::log::batch_view batch) override {
		written_records_ += batch.size;
		++write_calls_;
	}

	void flush() override {
		++flush_calls_;
	}

	[[nodiscard]] std::size_t written_records() const noexcept {
		return written_records_;
	}

private:
	std::size_t written_records_{};
	std::size_t write_calls_{};
	std::size_t flush_calls_{};
};

struct formatter_fixture final {
	formatter_fixture() {
		fields[0].name = "path";
		fields[0].value.type = modern::log::field_type::string;
		fields[0].value.storage.string = "terrain.bin";

		fields[1].name = "bytes";
		fields[1].value.type = modern::log::field_type::unsigned_integer;
		fields[1].value.storage.unsigned_integer = 524288;

		fields[2].name = "duration_ms";
		fields[2].value.type = modern::log::field_type::floating_point;
		fields[2].value.storage.floating_point = 8.9;

		fields[3].name = "cached";
		fields[3].value.type = modern::log::field_type::boolean;
		fields[3].value.storage.boolean = true;

		fields[4].name = "stream";
		fields[4].value.type = modern::log::field_type::string;
		fields[4].value.storage.string = "terrain";

		entry.timestamp_ns = 1780222530123456789ULL;
		entry.log_level = modern::log::level::info;
		entry.category = "assets";
		entry.event_name = "asset.loaded";
		entry.thread_id = 7;
		entry.task_id = 42;
		entry.trace_id = "trace-123";
		entry.span_id = "span-456";
		entry.traceparent = "00-trace-123-span-456-01";
		entry.message_template = "loaded asset";
		entry.fields = std::span<const modern::log::field>(fields.data(), fields.size());
	}

	std::array<modern::log::field, 5> fields{};
	modern::log::record entry{};
};

[[nodiscard]] std::size_t parse_size(std::string_view value) {
	std::size_t parsed{};
	const auto* begin = value.data();
	const auto* end = value.data() + value.size();
	const auto [next, error] = std::from_chars(begin, end, parsed);
	if (error != std::errc{} || next != end || parsed == 0) {
		throw std::invalid_argument("records must be a positive integer");
	}
	return parsed;
}

[[nodiscard]] benchmark_options parse_options(int argc, char** argv) {
	benchmark_options options{};

	for (int index = 1; index < argc; ++index) {
		const std::string_view argument{argv[index]};
		if (argument == "--help") {
			std::cout << "usage: modern_log_hot_path_benchmark [--records N]\n";
			std::exit(0);
		}

		if (argument == "--records") {
			if (index + 1 >= argc) {
				throw std::invalid_argument("--records requires a value");
			}

			options.records = parse_size(argv[++index]);
			continue;
		}

		throw std::invalid_argument("unknown argument: " + std::string(argument));
	}

	return options;
}

[[nodiscard]] std::uint64_t percentile(std::vector<std::uint64_t> samples, double fraction) {
	if (samples.empty()) {
		return 0;
	}

	std::sort(samples.begin(), samples.end());
	const auto last_index = samples.size() - 1;
	const auto index = static_cast<std::size_t>(static_cast<double>(last_index) * fraction);
	return samples[index];
}

[[nodiscard]] double records_per_second(std::size_t records, std::uint64_t elapsed_ns) {
	if (elapsed_ns == 0) {
		return 0.0;
	}

	return static_cast<double>(records) * 1'000'000'000.0 / static_cast<double>(elapsed_ns);
}

[[nodiscard]] double percent_change(double baseline, double candidate) {
	if (baseline == 0.0) {
		return 0.0;
	}

	return ((candidate - baseline) / baseline) * 100.0;
}

[[nodiscard]] double percent_reduction(std::uint64_t baseline, std::uint64_t candidate) {
	if (baseline == 0) {
		return 0.0;
	}

	return (1.0 - (static_cast<double>(candidate) / static_cast<double>(baseline))) * 100.0;
}

template <typename Formatter>
[[nodiscard]] formatter_result run_formatter_benchmark(
	std::string_view name,
	std::size_t records,
	Formatter& formatter,
	const modern::log::record& entry
) {
	std::size_t emitted_bytes{};
	const auto start = steady_clock::now();

	for (std::size_t index = 0; index < records; ++index) {
		emitted_bytes += formatter.format(entry).size();
	}

	const auto elapsed = std::chrono::duration_cast<nanoseconds>(steady_clock::now() - start).count();
	return formatter_result{
		.name = name,
		.records = records,
		.elapsed_ns = static_cast<std::uint64_t>(elapsed),
		.emitted_bytes = emitted_bytes,
	};
}

[[nodiscard]] async_result run_async_benchmark(
	std::string_view name,
	std::size_t records,
	async_config config
) {
	auto sink = std::make_shared<counting_sink>();
	auto logger = modern::log::async_logger::builder()
		.sink(sink)
		.queue_capacity(config.queue_capacity)
		.batch_limit(config.batch_limit)
		.wakeup_threshold(config.wakeup_threshold)
		.backpressure_policy(modern::log::backpressure_policy::block_producer)
		.flush_interval(config.flush_interval)
		.build();
	const auto category_logger = logger.category("bench");

	std::vector<std::uint64_t> enqueue_latencies;
	enqueue_latencies.reserve(records);

	const auto start = steady_clock::now();
	for (std::size_t index = 0; index < records; ++index) {
		const auto enqueue_start = steady_clock::now();
		category_logger.event("asset.loaded")
			.field("path", "terrain.bin")
			.field("bytes", std::uint64_t{524288})
			.field("duration_ms", 8.9)
			.field("cached", true)
			.field("iteration", static_cast<std::uint64_t>(index))
			.submit();
		const auto enqueue_elapsed = std::chrono::duration_cast<nanoseconds>(
			steady_clock::now() - enqueue_start
		).count();
		enqueue_latencies.push_back(static_cast<std::uint64_t>(enqueue_elapsed));
	}
	logger.shutdown();
	const auto elapsed = std::chrono::duration_cast<nanoseconds>(steady_clock::now() - start).count();
	const auto metrics = logger.metrics();

	if (metrics.dropped_records != 0
		|| metrics.enqueue_failures != 0
		|| metrics.enqueued_records != records
		|| metrics.written_records != records
		|| sink->written_records() != records) {
		throw std::runtime_error("benchmark run dropped or lost records");
	}

	return async_result{
		.name = name,
		.records = records,
		.elapsed_ns = static_cast<std::uint64_t>(elapsed),
		.enqueue_p50_ns = percentile(enqueue_latencies, 0.50),
		.enqueue_p95_ns = percentile(enqueue_latencies, 0.95),
		.sink_write_calls = metrics.sink_write_calls,
		.sink_flush_calls = metrics.sink_flush_calls,
		.high_water_mark = metrics.high_water_mark,
	};
}

void print_formatter_result(const formatter_result& result) {
	std::cout
		<< "formatter name=" << result.name
		<< " records=" << result.records
		<< " elapsed_ms=" << std::fixed << std::setprecision(3)
		<< (static_cast<double>(result.elapsed_ns) / 1'000'000.0)
		<< " records_per_sec=" << records_per_second(result.records, result.elapsed_ns)
		<< " emitted_bytes=" << result.emitted_bytes
		<< '\n';
}

void print_async_result(const async_result& result) {
	std::cout
		<< "async name=" << result.name
		<< " records=" << result.records
		<< " elapsed_ms=" << std::fixed << std::setprecision(3)
		<< (static_cast<double>(result.elapsed_ns) / 1'000'000.0)
		<< " records_per_sec=" << records_per_second(result.records, result.elapsed_ns)
		<< " enqueue_p50_ns=" << result.enqueue_p50_ns
		<< " enqueue_p95_ns=" << result.enqueue_p95_ns
		<< " sink_write_calls=" << result.sink_write_calls
		<< " sink_flush_calls=" << result.sink_flush_calls
		<< " high_water_mark=" << result.high_water_mark
		<< '\n';
}

} // namespace

int main(int argc, char** argv) {
	try {
		const auto options = parse_options(argc, argv);

		const formatter_fixture fixture{};
		modern::log::text_formatter text_formatter{};
		modern::log::json_formatter json_formatter{};

		const auto text_result = run_formatter_benchmark(
			"text",
			options.records,
			text_formatter,
			fixture.entry
		);
		const auto json_result = run_formatter_benchmark(
			"json",
			options.records,
			json_formatter,
			fixture.entry
		);

		const auto baseline = run_async_benchmark(
			"baseline",
			options.records,
			async_config{
				.queue_capacity = 2048,
				.batch_limit = 64,
				.wakeup_threshold = 1,
				.flush_interval = std::chrono::milliseconds{0},
			}
		);
		const auto tuned = run_async_benchmark(
			"tuned",
			options.records,
			async_config{
				.queue_capacity = 8192,
				.batch_limit = 256,
				.wakeup_threshold = 128,
				.flush_interval = std::chrono::milliseconds{10},
			}
		);

		print_formatter_result(text_result);
		print_formatter_result(json_result);
		print_async_result(baseline);
		print_async_result(tuned);

		std::cout
			<< "delta tuned_vs_baseline throughput_pct=" << std::fixed << std::setprecision(2)
			<< percent_change(
				records_per_second(baseline.records, baseline.elapsed_ns),
				records_per_second(tuned.records, tuned.elapsed_ns)
			)
			<< " p95_enqueue_pct="
			<< percent_change(
				static_cast<double>(baseline.enqueue_p95_ns),
				static_cast<double>(tuned.enqueue_p95_ns)
			)
			<< " sink_write_call_reduction_pct="
			<< percent_reduction(baseline.sink_write_calls, tuned.sink_write_calls)
			<< '\n';

		return 0;
	} catch (const std::exception& error) {
		std::cerr << "benchmark error: " << error.what() << '\n';
		return 1;
	}
}