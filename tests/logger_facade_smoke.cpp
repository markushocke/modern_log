import modern.log;

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

struct captured_record final {
    modern::log::level level{};
    std::string category;
    std::string message;
    std::uint64_t timestamp_ns{};
    std::uint64_t thread_id{};
};

class collecting_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        for (std::size_t index = 0; index < batch.size; ++index) {
            const auto& current = batch.data[index];
            records.push_back(captured_record{
                current.log_level,
                std::string(current.category),
                std::string(current.message_template),
                current.timestamp_ns,
                current.thread_id,
            });
        }
    }

    std::vector<captured_record> records{};
};

} // namespace

int main() {
    auto sink = std::make_shared<collecting_sink>();

    auto logger = modern::log::logger::builder()
        .sink(sink)
        .build();

    logger.info("engine started");
    logger.category("streaming").warn("budget exceeded");
    logger.error("gpu upload failed");

    if (sink->records.size() != 3) {
        return 1;
    }

    if (sink->records[0].level != modern::log::level::info) {
        return 1;
    }

    if (!sink->records[1].category.empty() && sink->records[1].category != "streaming") {
        return 1;
    }

    if (sink->records[1].category != "streaming") {
        return 1;
    }

    if (sink->records[2].level != modern::log::level::error) {
        return 1;
    }

    if (sink->records[0].message != "engine started") {
        return 1;
    }

    if (sink->records[0].timestamp_ns == 0 || sink->records[0].thread_id == 0) {
        return 1;
    }

    return 0;
}