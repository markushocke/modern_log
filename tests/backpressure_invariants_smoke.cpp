import modern.log;

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct captured_record final {
    modern::log::level level{};
    std::string message;
};

class blocking_collecting_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        std::unique_lock lock(mutex_);
        for (std::size_t index = 0; index < batch.size; ++index) {
            const auto& current = batch.data[index];
            records_.push_back(captured_record{
                current.log_level,
                std::string(current.message_template),
            });
        }

        if (!entered_) {
            entered_ = true;
            entered_cv_.notify_all();
            release_cv_.wait(lock, [this] { return released_; });
        }
    }

    [[nodiscard]] bool wait_until_entered(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return entered_cv_.wait_for(lock, timeout, [this] { return entered_; });
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }

        release_cv_.notify_all();
    }

    [[nodiscard]] std::vector<captured_record> records() const {
        std::lock_guard lock(mutex_);
        return records_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable entered_cv_{};
    std::condition_variable release_cv_{};
    bool entered_{};
    bool released_{};
    std::vector<captured_record> records_{};
};

[[nodiscard]] auto make_logger(
    const std::shared_ptr<blocking_collecting_sink>& sink,
    modern::log::backpressure_policy policy
) {
    return modern::log::async_logger::builder()
        .sink(sink)
        .queue_capacity(2)
        .batch_limit(1)
        .wakeup_threshold(1)
        .flush_interval(std::chrono::hours(24))
        .backpressure_policy(policy)
        .build();
}

[[nodiscard]] bool expect_messages(
    const std::vector<captured_record>& records,
    const std::vector<std::string>& expected
) {
    if (records.size() != expected.size()) {
        return false;
    }

    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (records[index].message != expected[index]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool drop_newest_preserves_queued_records() {
    auto sink = std::make_shared<blocking_collecting_sink>();
    auto logger = make_logger(sink, modern::log::backpressure_policy::drop_newest);

    logger.info("first");
    if (!sink->wait_until_entered(200ms)) {
        logger.shutdown();
        return false;
    }

    logger.info("second");
    logger.info("third");
    logger.info("fourth");

    sink->release();
    logger.shutdown();

    const auto metrics = logger.metrics();
    return metrics.dropped_records == 1
        && metrics.enqueue_failures == 1
        && metrics.high_water_mark == 2
        && metrics.written_records == 3
        && expect_messages(sink->records(), {"first", "second", "third"});
}

[[nodiscard]] bool drop_oldest_replaces_oldest_queued_record() {
    auto sink = std::make_shared<blocking_collecting_sink>();
    auto logger = make_logger(sink, modern::log::backpressure_policy::drop_oldest);

    logger.info("first");
    if (!sink->wait_until_entered(200ms)) {
        logger.shutdown();
        return false;
    }

    logger.info("second");
    logger.info("third");
    logger.info("fourth");

    sink->release();
    logger.shutdown();

    const auto metrics = logger.metrics();
    return metrics.dropped_records == 1
        && metrics.enqueue_failures == 0
        && metrics.high_water_mark == 2
        && metrics.written_records == 3
        && expect_messages(sink->records(), {"first", "third", "fourth"});
}

[[nodiscard]] bool block_producer_waits_for_capacity() {
    auto sink = std::make_shared<blocking_collecting_sink>();
    auto logger = make_logger(sink, modern::log::backpressure_policy::block_producer);

    logger.info("first");
    if (!sink->wait_until_entered(200ms)) {
        logger.shutdown();
        return false;
    }

    logger.info("second");
    logger.info("third");

    auto producer = std::async(std::launch::async, [&logger] {
        logger.info("fourth");
    });

    if (producer.wait_for(100ms) != std::future_status::timeout) {
        sink->release();
        logger.shutdown();
        return false;
    }

    sink->release();

    if (producer.wait_for(1s) != std::future_status::ready) {
        logger.shutdown();
        return false;
    }

    logger.shutdown();

    const auto metrics = logger.metrics();
    return metrics.dropped_records == 0
        && metrics.enqueue_failures == 0
        && metrics.high_water_mark == 2
        && metrics.written_records == 4
        && expect_messages(sink->records(), {"first", "second", "third", "fourth"});
}

[[nodiscard]] bool sample_debug_logs_prefers_non_debug_records() {
    auto sink = std::make_shared<blocking_collecting_sink>();
    auto logger = make_logger(sink, modern::log::backpressure_policy::sample_debug_logs);

    logger.info("first");
    if (!sink->wait_until_entered(200ms)) {
        logger.shutdown();
        return false;
    }

    logger.debug("debug-1");
    logger.trace("trace-2");
    logger.info("info-3");

    sink->release();
    logger.shutdown();

    const auto metrics = logger.metrics();
    const auto records = sink->records();
    if (records.size() != 3) {
        return false;
    }

    return metrics.dropped_records == 1
        && metrics.enqueue_failures == 0
        && metrics.written_records == 3
        && records[0].message == "first"
        && records[1].message == "trace-2"
        && records[2].message == "info-3"
        && records[1].level == modern::log::level::trace
        && records[2].level == modern::log::level::info;
}

[[nodiscard]] bool priority_preserve_errors_prefers_errors() {
    auto sink = std::make_shared<blocking_collecting_sink>();
    auto logger = make_logger(sink, modern::log::backpressure_policy::priority_preserve_errors);

    logger.info("first");
    if (!sink->wait_until_entered(200ms)) {
        logger.shutdown();
        return false;
    }

    logger.info("info-1");
    logger.warn("warn-2");
    logger.error("error-3");

    sink->release();
    logger.shutdown();

    const auto metrics = logger.metrics();
    const auto records = sink->records();
    if (records.size() != 3) {
        return false;
    }

    return metrics.dropped_records == 1
        && metrics.enqueue_failures == 0
        && metrics.written_records == 3
        && records[0].message == "first"
        && records[1].message == "warn-2"
        && records[2].message == "error-3"
        && records[1].level == modern::log::level::warn
        && records[2].level == modern::log::level::error;
}

} // namespace

int main() {
    if (!drop_newest_preserves_queued_records()) {
        return 1;
    }

    if (!drop_oldest_replaces_oldest_queued_record()) {
        return 1;
    }

    if (!block_producer_waits_for_capacity()) {
        return 1;
    }

    if (!sample_debug_logs_prefers_non_debug_records()) {
        return 1;
    }

    if (!priority_preserve_errors_prefers_errors()) {
        return 1;
    }

    return 0;
}