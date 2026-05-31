import modern.log;

#include <memory>
#include <string>
#include <vector>

namespace {

struct captured_field final {
    std::string name;
    modern::log::field_type type{};
    std::string string_value;
    std::uint64_t unsigned_value{};
    double floating_value{};
    bool bool_value{};
};

struct captured_event final {
    std::string category;
    std::string event_name;
    std::vector<captured_field> fields;
};

class collecting_sink final : public modern::log::sink {
public:
    void write(modern::log::batch_view batch) override {
        for (std::size_t index = 0; index < batch.size; ++index) {
            const auto& current = batch.data[index];

            captured_event event{};
            event.category = std::string(current.category);
            event.event_name = std::string(current.event_name);

            for (const auto& current_field : current.fields) {
                captured_field field{};
                field.name = std::string(current_field.name);
                field.type = current_field.value.type;

                switch (current_field.value.type) {
                case modern::log::field_type::string:
                    field.string_value = std::string(current_field.value.storage.string);
                    break;
                case modern::log::field_type::unsigned_integer:
                    field.unsigned_value = current_field.value.storage.unsigned_integer;
                    break;
                case modern::log::field_type::floating_point:
                    field.floating_value = current_field.value.storage.floating_point;
                    break;
                case modern::log::field_type::boolean:
                    field.bool_value = current_field.value.storage.boolean;
                    break;
                default:
                    break;
                }

                event.fields.push_back(std::move(field));
            }

            events.push_back(std::move(event));
        }
    }

    std::vector<captured_event> events{};
};

} // namespace

int main() {
    auto sink = std::make_shared<collecting_sink>();

    auto logger = modern::log::logger::builder()
        .sink(sink)
        .build();

    logger.category("assets")
        .event("asset.loaded")
        .field("path", "ship.mesh")
        .field("bytes", std::uint64_t{65536})
        .field("duration_ms", 3.5)
        .field("cached", true)
        .submit();

    if (sink->events.size() != 1) {
        return 1;
    }

    const auto& event = sink->events.front();
    if (event.category != "assets" || event.event_name != "asset.loaded") {
        return 1;
    }

    if (event.fields.size() != 4) {
        return 1;
    }

    if (event.fields[0].type != modern::log::field_type::string || event.fields[0].string_value != "ship.mesh") {
        return 1;
    }

    if (event.fields[1].type != modern::log::field_type::unsigned_integer || event.fields[1].unsigned_value != 65536) {
        return 1;
    }

    if (event.fields[2].type != modern::log::field_type::floating_point || event.fields[2].floating_value != 3.5) {
        return 1;
    }

    if (event.fields[3].type != modern::log::field_type::boolean || !event.fields[3].bool_value) {
        return 1;
    }

    return 0;
}