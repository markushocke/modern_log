import modern.log;

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace {

struct captured_record final {
    modern::log::level level{};
    std::string category;
    std::string event_name;
    std::string message;
};

class collecting_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        for (std::size_t index = 0; index < batch.size; ++index) {
            const auto& current = batch.data[index];
            records.push_back(captured_record{
                current.log_level,
                std::string(current.category),
                std::string(current.event_name),
                std::string(current.message_template),
            });
        }
    }

    std::vector<captured_record> records{};
};

} // namespace

int main() {
    auto sink = std::make_shared<collecting_sink>();

    auto logger = modern::log::async_logger::builder()
        .sink(sink)
        .batch_limit(8)
        .wakeup_threshold(4)
        .flush_interval(std::chrono::hours(24))
        .build();

    logger.info("engine started");
    logger.category("streaming").warn("budget exceeded");
    logger.category("assets")
        .event("asset.loaded")
        .field("bytes", std::uint64_t{64})
        .submit();

    logger.flush();
    logger.shutdown();

    if (sink->records.size() != 3) {
        return 1;
    }

    if (sink->records[0].level != modern::log::level::info || sink->records[0].message != "engine started") {
        return 1;
    }

    if (sink->records[1].category != "streaming" || sink->records[1].level != modern::log::level::warn) {
        return 1;
    }

    if (sink->records[2].category != "assets" || sink->records[2].event_name != "asset.loaded") {
        return 1;
    }

    return 0;
}