import modern.log;

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

struct captured_otel_record final {
	std::string severity_text{};
	modern::log::otel_severity_number severity_number{modern::log::otel_severity_number::info};
	std::string body{};
	std::string logger_name{};
	std::string event_name{};
	std::uint64_t thread_id{};
	std::uint64_t task_id{};
	std::string trace_id{};
	std::string span_id{};
	std::string traceparent{};
	std::uint32_t dropped_count{};
	std::vector<std::pair<std::string, std::string>> attributes{};
};

class collecting_otel_exporter final : public modern::log::otel_log_exporter {
public:
	void export_logs(std::span<const modern::log::otel_log_record_view> records) override {
		for (const auto& record : records) {
			captured_otel_record copy{};
			copy.severity_text = std::string(record.severity_text);
			copy.severity_number = record.severity_number;
			copy.body = std::string(record.body);
			copy.logger_name = std::string(record.logger_name);
			copy.event_name = std::string(record.event_name);
			copy.thread_id = record.thread_id;
			copy.task_id = record.task_id;
			copy.trace_id = std::string(record.trace_id);
			copy.span_id = std::string(record.span_id);
			copy.traceparent = std::string(record.traceparent);
			copy.dropped_count = record.dropped_count;

			for (const auto& attribute : record.attributes) {
				copy.attributes.emplace_back(std::string(attribute.name), render(attribute.value));
			}

			records_.push_back(std::move(copy));
		}
	}

	void flush() override {
		++flush_calls;
	}

	[[nodiscard]] const std::vector<captured_otel_record>& records() const noexcept {
		return records_;
	}

	std::size_t flush_calls{};

private:
	[[nodiscard]] static std::string render(const modern::log::field_value& value) {
		switch (value.type) {
		case modern::log::field_type::signed_integer:
			return std::to_string(value.storage.signed_integer);
		case modern::log::field_type::unsigned_integer:
			return std::to_string(value.storage.unsigned_integer);
		case modern::log::field_type::floating_point:
			return std::to_string(value.storage.floating_point);
		case modern::log::field_type::boolean:
			return value.storage.boolean ? "true" : "false";
		case modern::log::field_type::string:
			return std::string(value.storage.string);
		case modern::log::field_type::bytes:
			return std::to_string(value.storage.bytes.size);
		}

		return {};
	}

	std::vector<captured_otel_record> records_{};
};

[[nodiscard]] bool has_attribute(
	const captured_otel_record& record,
	std::string_view name,
	std::string_view value
) {
	for (const auto& [current_name, current_value] : record.attributes) {
		if (current_name == name && current_value == value) {
			return true;
		}
	}

	return false;
}

} // namespace

int main() {
	using namespace modern::log;

	auto exporter = std::make_shared<collecting_otel_exporter>();
	auto target = std::make_shared<otel_sink>(exporter);
	auto logger = logger::builder()
		.sink(target)
		.build();

	logger.category("orders").warn("otel relay lagging");
	logger.category("orders")
		.event("order.shipped")
		.field("order_id", std::uint64_t{42})
		.field("expedited", true)
		.submit();

	if (exporter->records().size() != 2 || exporter->flush_calls != 2) {
		return 1;
	}

	const auto& warn_record = exporter->records().front();
	if (warn_record.severity_number != otel_severity_number::warn) {
		return 1;
	}

	if (warn_record.severity_text != "WARN") {
		return 1;
	}

	if (warn_record.body != "otel relay lagging") {
		return 1;
	}

	if (warn_record.logger_name != "orders") {
		return 1;
	}

	const auto& event_record = exporter->records().back();
	if (event_record.severity_number != otel_severity_number::info) {
		return 1;
	}

	if (event_record.event_name != "order.shipped") {
		return 1;
	}

	if (!has_attribute(event_record, "order_id", "42")) {
		return 1;
	}

	if (!has_attribute(event_record, "expedited", "true")) {
		return 1;
	}

	bool threw = false;
	try {
		[[maybe_unused]] otel_sink invalid{std::shared_ptr<otel_log_exporter>{}};
	} catch (const std::invalid_argument&) {
		threw = true;
	}

	return threw ? 0 : 1;
}