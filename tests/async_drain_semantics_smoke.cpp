import modern.log;

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace {

class counting_sink final : public modern::log::sink {
public:
	void write(modern::log::batch_view batch) override {
		{
			std::lock_guard lock(mutex_);
			write_calls_ += 1;
			batch_sizes_.push_back(batch.size);
		}

		cv_.notify_all();
	}

	void flush() override {
		std::lock_guard lock(mutex_);
		flush_calls_ += 1;
	}

	[[nodiscard]] bool wait_for_write_calls(std::size_t expected, std::chrono::milliseconds timeout) {
		std::unique_lock lock(mutex_);
		return cv_.wait_for(lock, timeout, [this, expected] {
			return write_calls_ >= expected;
		});
	}

	[[nodiscard]] std::size_t write_calls() const {
		std::lock_guard lock(mutex_);
		return write_calls_;
	}

	[[nodiscard]] std::size_t flush_calls() const {
		std::lock_guard lock(mutex_);
		return flush_calls_;
	}

	[[nodiscard]] std::vector<std::size_t> batch_sizes() const {
		std::lock_guard lock(mutex_);
		return batch_sizes_;
	}

private:
	mutable std::mutex mutex_{};
	std::condition_variable cv_{};
	std::size_t write_calls_{};
	std::size_t flush_calls_{};
	std::vector<std::size_t> batch_sizes_{};
};

} // namespace

int main() {
	using namespace std::chrono_literals;

	auto sink = std::make_shared<counting_sink>();

	auto logger = modern::log::async_logger::builder()
		.sink(sink)
		.batch_limit(8)
		.wakeup_threshold(8)
		.flush_interval(24h)
		.build();

	logger.info("one");
	logger.info("two");
	logger.info("three");
	logger.flush();

	const bool wrote = sink->wait_for_write_calls(1, 250ms);
	logger.shutdown();

	if (!wrote) {
		return 1;
	}

	if (sink->write_calls() != 1) {
		return 1;
	}

	if (sink->flush_calls() != 1) {
		return 1;
	}

	const auto batch_sizes = sink->batch_sizes();
	if (batch_sizes.size() != 1 || batch_sizes.front() != 3) {
		return 1;
	}

	return 0;
}