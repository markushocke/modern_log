import modern.log;

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {

class blocking_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        {
            std::lock_guard lock(mutex_);
            entered_ = true;
            writes_ += batch.size;
        }

        entered_cv_.notify_all();

        std::unique_lock lock(mutex_);
        release_cv_.wait(lock, [this] { return released_; });
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

    [[nodiscard]] std::size_t writes() const {
        std::lock_guard lock(mutex_);
        return writes_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable entered_cv_{};
    std::condition_variable release_cv_{};
    bool entered_{};
    bool released_{};
    std::size_t writes_{};
};

} // namespace

int main() {
    auto sink = std::make_shared<blocking_sink>();

    auto logger = modern::log::async_logger::builder()
        .sink(sink)
        .queue_capacity(2)
        .batch_limit(1)
        .wakeup_threshold(1)
        .backpressure_policy(modern::log::backpressure_policy::drop_newest)
        .build();

    logger.info("first");
    if (!sink->wait_until_entered(std::chrono::milliseconds{200})) {
        return 1;
    }

    logger.info("second");
    logger.info("third");
    logger.info("fourth");

    const auto metrics = logger.metrics();
    if (metrics.enqueue_attempts != 4
        || metrics.enqueued_records != 3
        || metrics.dropped_records != 1
        || metrics.enqueue_failures != 1
        || metrics.current_queue_depth != 2
        || metrics.high_water_mark != 2) {
        sink->release();
        logger.shutdown();
        return 1;
    }

    sink->release();
    logger.shutdown();

    const auto drained_metrics = logger.metrics();
    if (drained_metrics.current_queue_depth != 0
        || drained_metrics.written_records != 3
        || drained_metrics.sink_write_calls != 3
        || drained_metrics.sink_flush_calls != 3
        || drained_metrics.flush_count != 0
        || drained_metrics.write_failures != 0) {
        return 1;
    }

    return sink->writes() >= 1 ? 0 : 1;
}