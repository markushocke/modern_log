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
    std::vector<std::pair<std::string, std::string>> string_fields;
    std::vector<std::pair<std::string, std::uint64_t>> unsigned_fields;
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

            auto& stored = records.back();
            for (const auto& current_field : current.fields) {
                if (current_field.value.type == modern::log::field_type::string) {
                    stored.string_fields.emplace_back(
                        std::string(current_field.name),
                        std::string(current_field.value.storage.string)
                    );
                }

                if (current_field.value.type == modern::log::field_type::unsigned_integer) {
                    stored.unsigned_fields.emplace_back(
                        std::string(current_field.name),
                        current_field.value.storage.unsigned_integer
                    );
                }
            }
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
        .field("path", "content/packs/terrain/ship.mesh/high")
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

    if (sink->records[2].string_fields.size() != 1
        || sink->records[2].string_fields[0].first != "path"
        || sink->records[2].string_fields[0].second != "content/packs/terrain/ship.mesh/high") {
        return 1;
    }

    if (sink->records[2].unsigned_fields.size() != 1
        || sink->records[2].unsigned_fields[0].first != "bytes"
        || sink->records[2].unsigned_fields[0].second != 64) {
        return 1;
    }

    return 0;
}