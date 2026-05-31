import modern.log;
import modern.runtime;
import modern.trace;

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct captured_record final {
	std::string category;
	std::string message;
	std::string trace_id;
	std::string span_id;
	std::string traceparent;
};

class notifying_sink final : public modern::log::sink {
public:
	void write(modern::log::batch_view batch) override {
		{
			std::lock_guard lock(mutex_);
			for (std::size_t index = 0; index < batch.size; ++index) {
				const auto& current = batch.data[index];
				records_.push_back(captured_record{
					std::string(current.category),
					std::string(current.message_template),
					std::string(current.trace_id),
					std::string(current.span_id),
					std::string(current.traceparent),
				});
			}
		}

		cv_.notify_all();
	}

	[[nodiscard]] bool wait_for_records(std::size_t expected, std::chrono::milliseconds timeout) {
		std::unique_lock lock(mutex_);
		return cv_.wait_for(lock, timeout, [this, expected] {
			return records_.size() >= expected;
		});
	}

	[[nodiscard]] std::vector<captured_record> snapshot() const {
		std::lock_guard lock(mutex_);
		return records_;
	}

private:
	mutable std::mutex mutex_{};
	std::condition_variable cv_{};
	std::vector<captured_record> records_{};
};

} // namespace

int main() {
	using namespace std::chrono_literals;

	modern::trace::TraceContext trace{};
	trace.trace_id[15] = std::byte{0x47};
	trace.span_id[7] = std::byte{0xb7};

	const auto path = std::filesystem::temp_directory_path() / "modern_log_external_runtime_io_smoke.log";
	std::filesystem::remove(path);

	modern::thread_pool pool{2};
	auto sink = std::make_shared<notifying_sink>();
	auto file = std::make_shared<modern::log::file_sink>(path);

	auto logger = modern::log::async_logger::builder()
		.sink(sink)
		.sink(file)
		.batch_limit(8)
		.wakeup_threshold(8)
		.flush_interval(10ms)
		.scheduler(pool.get_scheduler())
		.build();

	const auto cleanup = [&] {
		logger.shutdown();
		pool.shutdown();
		pool.join();
	};

	{
		modern::runtime::TraceContextScope scope{trace};
		logger.category("runtime").info("runtime scheduled");
	}

	if (!sink->wait_for_records(1, 1s)) {
		cleanup();
		return 1;
	}

	auto records = sink->snapshot();
	cleanup();

	if (records.size() != 1) {
		return 1;
	}

	const auto& record = records.front();
	if (record.category != "runtime" || record.message != "runtime scheduled") {
		return 1;
	}

	if (record.trace_id.empty() || record.span_id.empty() || record.traceparent.empty()) {
		return 1;
	}

	std::ifstream input(path);
	std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	std::filesystem::remove(path);

	if (contents.find("traceparent=" + record.traceparent) == std::string::npos) {
		return 1;
	}

	if (contents.find("message=\"runtime scheduled\"") == std::string::npos) {
		return 1;
	}

	return 0;
}