import modern.log.format;

#include <array>
#include <span>
#include <string>

int main() {
    using namespace modern::log;

    field fields[2]{};

    fields[0].name = "bytes";
    fields[0].value.type = field_type::unsigned_integer;
    fields[0].value.storage.unsigned_integer = 65536;

    fields[1].name = "cached";
    fields[1].value.type = field_type::boolean;
    fields[1].value.storage.boolean = true;

    record entry{};
    entry.timestamp_ns = 42;
    entry.log_level = level::warn;
    entry.category = "streaming";
    entry.event_name = "asset.loaded";
    entry.trace_id = "trace-1";
    entry.message_template = "budget exceeded";
    entry.fields = std::span<const field>(fields, 2);

    text_formatter formatter{};
    const auto text = formatter.format(entry);
    json_formatter json{};
    const auto json_text = json.format(entry);

    const std::string expected =
        "timestamp=1970-01-01T00:00:00.000000042Z ts=42 level=warn category=streaming event=asset.loaded trace_id=trace-1 message=\"budget exceeded\" bytes=65536 cached=true";

    const std::string expected_json =
        "{\"timestamp\":\"1970-01-01T00:00:00.000000042Z\",\"timestamp_ns\":42,\"level\":\"warn\",\"category\":\"streaming\",\"event\":\"asset.loaded\",\"trace_id\":\"trace-1\",\"message\":\"budget exceeded\",\"bytes\":65536,\"cached\":true}";

    return text == expected && json_text == expected_json ? 0 : 1;
}