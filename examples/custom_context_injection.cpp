import modern.log;

#include <memory>

class request_context_provider final : public modern::log::context_provider {
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

int main() {
	request_context_provider provider;
	modern::log::scoped_context_provider scope{&provider};

	auto logger = modern::log::logger::builder()
		.sink(std::make_shared<modern::log::console_sink>())
		.build();

	logger.info("request accepted");

	return 0;
}