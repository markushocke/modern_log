import modern.log;

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class collecting_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        for (std::size_t index = 0; index < batch.size; ++index) {
            records_.emplace_back(batch.data[index].message_template);
        }
    }

    [[nodiscard]] const std::vector<std::string>& records() const noexcept {
        return records_;
    }

private:
    std::vector<std::string> records_{};
};

class throwing_write_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view) override {
        throw std::runtime_error("write failed");
    }
};

class throwing_flush_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        writes_ += batch.size;
    }

    void flush() override {
        throw std::runtime_error("flush failed");
    }

    [[nodiscard]] std::size_t writes() const noexcept {
        return writes_;
    }

private:
    std::size_t writes_{};
};

[[nodiscard]] auto make_logger(
    const std::shared_ptr<modern::log::sink>& failing_sink,
    const std::shared_ptr<collecting_sink>& collecting
) {
    return modern::log::async_logger::builder()
        .sink(failing_sink)
        .sink(collecting)
        .queue_capacity(8)
        .batch_limit(8)
        .wakeup_threshold(8)
        .flush_interval(std::chrono::hours(24))
        .build();
}

[[nodiscard]] bool write_failures_increment_counter_and_do_not_abort_other_sinks() {
    auto collecting = std::make_shared<collecting_sink>();
    auto failing = std::make_shared<throwing_write_sink>();
    auto logger = make_logger(failing, collecting);

    logger.info("first");
    logger.info("second");
    logger.flush();
    logger.shutdown();

    const auto metrics = logger.metrics();
    return metrics.enqueue_attempts == 2
        && metrics.enqueued_records == 2
        && metrics.current_queue_depth == 0
        && metrics.sink_write_calls == 2
        && metrics.sink_flush_calls == 1
        && metrics.write_failures == 1
        && metrics.written_records == 2
        && metrics.flush_count == 1
        && collecting->records().size() == 2
        && collecting->records()[0] == "first"
        && collecting->records()[1] == "second";
}

[[nodiscard]] bool flush_failures_increment_counter() {
    auto collecting = std::make_shared<collecting_sink>();
    auto failing = std::make_shared<throwing_flush_sink>();
    auto logger = make_logger(failing, collecting);

    logger.info("first");
    logger.flush();
    logger.shutdown();

    const auto metrics = logger.metrics();
    return metrics.enqueue_attempts == 1
        && metrics.enqueued_records == 1
        && metrics.current_queue_depth == 0
        && metrics.sink_write_calls == 2
        && metrics.sink_flush_calls == 2
        && metrics.write_failures == 1
        && metrics.written_records == 1
        && metrics.flush_count == 1
        && failing->writes() == 1
        && collecting->records().size() == 1
        && collecting->records()[0] == "first";
}

} // namespace

int main() {
    if (!write_failures_increment_counter_and_do_not_abort_other_sinks()) {
        return 1;
    }

    if (!flush_failures_increment_counter()) {
        return 1;
    }

    return 0;
}