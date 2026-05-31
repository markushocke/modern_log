import modern.log;

#include <iostream>
#include <memory>
#include <string>

namespace {

class stdout_remote_writer final : public modern::log::remote_writer {
public:
	void send(std::string payload) override {
		std::cout << "POST /logs/ingest\n" << payload;
		if (payload.empty() || payload.back() != '\n') {
			std::cout << '\n';
		}
	}

	void flush() override {
		std::cout.flush();
	}
};

} // namespace

int main() {
	auto transport = std::make_shared<stdout_remote_writer>();
	auto sink = std::make_shared<modern::log::remote_sink>(transport);

	auto logger = modern::log::logger::builder()
		.sink(sink)
		.build();

	logger.category("orders")
		.event("order.shipped")
		.field("order_id", std::uint64_t{42})
		.field("status", "sent")
		.submit();

	logger.warn("remote relay latency is rising");
	return 0;
}