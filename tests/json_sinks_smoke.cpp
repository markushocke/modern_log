import modern.log;

#include <memory>
#include <sstream>
#include <string>

int main() {
	using namespace modern::log;

	std::ostringstream json_buffer;
	auto json = std::make_shared<json_sink>(json_buffer);

	auto json_logger = logger::builder()
		.sink(json)
		.build();

	json_logger.event("asset.loaded")
		.field("bytes", 65536)
		.field("cached", true)
		.submit();

	const auto json_text = json_buffer.str();
	if (json_text.find("\"timestamp\":\"") == std::string::npos) {
		return 1;
	}

	if (json_text.find("\"timestamp_ns\":") == std::string::npos) {
		return 1;
	}

	if (json_text.find("\"event\":\"asset.loaded\"") == std::string::npos) {
		return 1;
	}

	if (json_text.find("\"bytes\":65536") == std::string::npos) {
		return 1;
	}

	if (json_text.find("\"cached\":true") == std::string::npos) {
		return 1;
	}

	std::ostringstream stderr_buffer;
	auto stderr_target = std::make_shared<stderr_sink>(stderr_buffer);

	auto stderr_logger = logger::builder()
		.sink(stderr_target)
		.build();

	stderr_logger.error("sink degraded");

	const auto stderr_text = stderr_buffer.str();
	if (stderr_text.find("timestamp=") == std::string::npos) {
		return 1;
	}

	if (stderr_text.find("level=error") == std::string::npos) {
		return 1;
	}

	if (stderr_text.find("message=\"sink degraded\"") == std::string::npos) {
		return 1;
	}

	return 0;
}