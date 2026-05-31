import modern.log;

#include <memory>
#include <string>
#include <vector>

namespace {

class collecting_remote_writer final : public modern::log::remote_writer {
public:
	void send(std::string payload) override {
		payloads.push_back(std::move(payload));
	}

	void flush() override {
		++flush_calls;
	}

	std::vector<std::string> payloads{};
	std::size_t flush_calls{};
};

} // namespace

int main() {
	using namespace modern::log;

	auto json_writer = std::make_shared<collecting_remote_writer>();
	auto json_target = std::make_shared<remote_sink>(json_writer);
	auto json_logger = logger::builder()
		.sink(json_target)
		.build();

	json_logger.category("orders")
		.event("order.shipped")
		.field("order_id", std::uint64_t{42})
		.field("status", "sent")
		.submit();

	if (json_writer->payloads.size() != 1 || json_writer->flush_calls != 1) {
		return 1;
	}

	const auto& json_payload = json_writer->payloads.front();
	if (json_payload.find("\"category\":\"orders\"") == std::string::npos) {
		return 1;
	}

	if (json_payload.find("\"event\":\"order.shipped\"") == std::string::npos) {
		return 1;
	}

	if (json_payload.find("\"order_id\":42") == std::string::npos) {
		return 1;
	}

	if (json_payload.find("\"status\":\"sent\"") == std::string::npos) {
		return 1;
	}

	auto text_writer = std::make_shared<collecting_remote_writer>();
	auto text_target = std::make_shared<remote_sink>(text_writer, remote_payload_format::text);
	auto text_logger = logger::builder()
		.sink(text_target)
		.build();

	text_logger.warn("remote degraded");

	if (text_writer->payloads.size() != 1 || text_writer->flush_calls != 1) {
		return 1;
	}

	const auto& text_payload = text_writer->payloads.front();
	if (text_payload.find("level=warn") == std::string::npos) {
		return 1;
	}

	if (text_payload.find("message=\"remote degraded\"") == std::string::npos) {
		return 1;
	}

	bool threw = false;
	try {
		[[maybe_unused]] remote_sink invalid{std::shared_ptr<remote_writer>{}};
	} catch (const std::invalid_argument&) {
		threw = true;
	}

	return threw ? 0 : 1;
}