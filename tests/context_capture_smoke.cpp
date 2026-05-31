import modern.log;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

struct captured_record final {
	std::uint64_t thread_id{};
	std::uint64_t task_id{};
	const void* trace_context{};
	std::string trace_id;
	std::string span_id;
	std::string traceparent;
	std::string event_name;
	std::string message;
	std::size_t field_count{};
};

class collecting_sink final : public modern::log::sink {
public:
	void write(modern::log::batch_view batch) override {
		for (std::size_t index = 0; index < batch.size; ++index) {
			const auto& current = batch.data[index];
			records.push_back(captured_record{
				current.thread_id,
				current.task_id,
				current.trace_context.native_context,
				std::string(current.trace_id),
				std::string(current.span_id),
				std::string(current.traceparent),
				std::string(current.event_name),
				std::string(current.message_template),
				current.fields.size(),
			});
		}
	}

	std::vector<captured_record> records{};
};

class fixed_context_provider final : public modern::log::context_provider {
public:
	[[nodiscard]] modern::log::context_snapshot capture() noexcept override {
		modern::log::context_snapshot snapshot{};
		snapshot.thread_id = 11;
		snapshot.task_id = 42;
		snapshot.trace_context.native_context = this;
		snapshot.trace_id = "4bf92f3577b34da6a3ce929d0e0e4736";
		snapshot.span_id = "00f067aa0ba902b7";
		snapshot.traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
		return snapshot;
	}
};

[[nodiscard]] bool matches(const captured_record& record, const void* expected_context, const char* expected_message) {
	return record.thread_id == 11
		&& record.task_id == 42
		&& record.trace_context == expected_context
		&& record.trace_id == "4bf92f3577b34da6a3ce929d0e0e4736"
		&& record.span_id == "00f067aa0ba902b7"
		&& record.traceparent == "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
		&& record.event_name.empty()
		&& record.message == expected_message;
}

[[nodiscard]] bool matches_event(const captured_record& record, const void* expected_context, const char* expected_event) {
	return record.thread_id == 11
		&& record.task_id == 42
		&& record.trace_context == expected_context
		&& record.trace_id == "4bf92f3577b34da6a3ce929d0e0e4736"
		&& record.span_id == "00f067aa0ba902b7"
		&& record.traceparent == "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"
		&& record.event_name == expected_event
		&& record.message.empty()
		&& record.field_count == 1;
}

} // namespace

int main() {
	fixed_context_provider provider;
	modern::log::scoped_context_provider scope{&provider};

	auto sync_sink = std::make_shared<collecting_sink>();
	auto async_sink = std::make_shared<collecting_sink>();

	auto sync_logger = modern::log::logger::builder()
		.sink(sync_sink)
		.build();

	auto async_logger = modern::log::async_logger::builder()
		.sink(async_sink)
		.build();

	sync_logger.info("sync");
	sync_logger.event("sync.event").field("attempt", std::uint64_t{1}).submit();
	async_logger.info("async");
	async_logger.event("async.event").field("attempt", std::uint64_t{1}).submit();
	async_logger.flush();
	async_logger.shutdown();

	if (sync_sink->records.size() != 2 || async_sink->records.size() != 2) {
		return 1;
	}

	if (!matches(sync_sink->records.front(), &provider, "sync")) {
		return 1;
	}

	if (!matches_event(sync_sink->records.back(), &provider, "sync.event")) {
		return 1;
	}

	if (!matches(async_sink->records.front(), &provider, "async")) {
		return 1;
	}

	if (!matches_event(async_sink->records.back(), &provider, "async.event")) {
		return 1;
	}

	return 0;
}