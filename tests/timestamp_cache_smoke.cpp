import modern.log.format;

#include <string>

int main() {
	using namespace modern::log;

	record first{};
	first.timestamp_ns = 42;
	first.log_level = level::info;
	first.message_template = "first";

	record second{};
	second.timestamp_ns = 142;
	second.log_level = level::debug;
	second.message_template = "second";

	record third{};
	third.timestamp_ns = 1'000'000'042;
	third.log_level = level::warn;
	third.message_template = "third";

	text_formatter text{};
	const auto first_text = text.format(first);
	const auto second_text = text.format(second);
	const auto third_text = text.format(third);

	json_formatter json{};
	const auto second_json = json.format(second);
	const auto third_json = json.format(third);

	return first_text.find("timestamp=1970-01-01T00:00:00.000000042Z ts=42") == 0
		&& second_text.find("timestamp=1970-01-01T00:00:00.000000142Z ts=142") == 0
		&& third_text.find("timestamp=1970-01-01T00:00:01.000000042Z ts=1000000042") == 0
		&& second_json.find("\"timestamp\":\"1970-01-01T00:00:00.000000142Z\"") != std::string::npos
		&& third_json.find("\"timestamp\":\"1970-01-01T00:00:01.000000042Z\"") != std::string::npos
		? 0
		: 1;
}