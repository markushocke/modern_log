import modern.log;
import modern.runtime;
import modern.trace;

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>

using namespace std::chrono_literals;

int main() {
	modern::trace::TraceContext trace{};
	trace.trace_id[15] = std::byte{0x47};
	trace.span_id[7] = std::byte{0xb7};

	modern::thread_pool pool{2};
	auto logger = modern::log::async_logger::builder()
		.sink(std::make_shared<modern::log::json_sink>())
		.sink(std::make_shared<modern::log::file_sink>(std::filesystem::path{"runtime.log"}))
		.batch_limit(8)
		.wakeup_threshold(8)
		.flush_interval(10ms)
		.scheduler(pool.get_scheduler())
		.build();

	{
		modern::runtime::TraceContextScope scope{trace};
		logger.category("runtime").info("runtime scheduled");
	}

	logger.flush();
	logger.shutdown();
	pool.shutdown();
	pool.join();

	return 0;
}