import modern.log;

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>

namespace {

void write_field_value(std::ostream& stream, const modern::log::field_value& value) {
	switch (value.type) {
	case modern::log::field_type::signed_integer:
		stream << value.storage.signed_integer;
		break;
	case modern::log::field_type::unsigned_integer:
		stream << value.storage.unsigned_integer;
		break;
	case modern::log::field_type::floating_point:
		stream << value.storage.floating_point;
		break;
	case modern::log::field_type::boolean:
		stream << (value.storage.boolean ? "true" : "false");
		break;
	case modern::log::field_type::string:
		stream << value.storage.string;
		break;
	case modern::log::field_type::bytes:
		stream << "<bytes:" << value.storage.bytes.size << ">";
		break;
	}
}

class stdout_otel_exporter final : public modern::log::otel_log_exporter {
public:
	void export_logs(std::span<const modern::log::otel_log_record_view> records) override {
		for (const auto& record : records) {
			std::cout << "severity=" << record.severity_text;
			if (!record.logger_name.empty()) {
				std::cout << " logger=" << record.logger_name;
			}
			if (!record.event_name.empty()) {
				std::cout << " event=" << record.event_name;
			}
			if (!record.body.empty()) {
				std::cout << " body=\"" << record.body << "\"";
			}

			for (const auto& attribute : record.attributes) {
				std::cout << ' ' << attribute.name << '=';
				write_field_value(std::cout, attribute.value);
			}

			std::cout << '\n';
		}
	}

	void flush() override {
		std::cout.flush();
	}
};

} // namespace

int main() {
	auto exporter = std::make_shared<stdout_otel_exporter>();
	auto sink = std::make_shared<modern::log::otel_sink>(exporter);

	auto logger = modern::log::logger::builder()
		.sink(sink)
		.build();

	logger.category("orders").warn("otel relay lagging");
	logger.category("orders")
		.event("order.shipped")
		.field("order_id", std::uint64_t{42})
		.field("status", "sent")
		.submit();

	return 0;
}