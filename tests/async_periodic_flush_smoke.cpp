import modern.log;

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {

class notifying_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        {
            std::lock_guard lock(mutex_);
            writes_ += batch.size;
        }

        cv_.notify_all();
    }

    [[nodiscard]] bool wait_for_writes(std::size_t expected, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, expected] { return writes_ >= expected; });
    }

private:
    std::mutex mutex_{};
    std::condition_variable cv_{};
    std::size_t writes_{};
};

} // namespace

int main() {
    auto sink = std::make_shared<notifying_sink>();

    auto logger = modern::log::async_logger::builder()
        .sink(sink)
        .batch_limit(8)
        .wakeup_threshold(4)
        .flush_interval(std::chrono::milliseconds{5})
        .build();

    logger.info("runtime started");

    const bool flushed = sink->wait_for_writes(1, std::chrono::milliseconds{250});
    logger.shutdown();

    return flushed ? 0 : 1;
}