import modern.log;

#include <string_view>
#include <memory>

int main() {
	using namespace modern::log;

	auto machine_logger = logger::builder()
		.sink(std::make_shared<json_sink>())
		.build();

	auto operator_logger = logger::builder()
		.sink(std::make_shared<stderr_sink>())
		.build();

	for (int step = 0; step != 3; ++step) {
		const std::string_view status = step == 2 ? "complete" : "running";

		machine_logger.event("deployment.progress")
			.field("service", "catalog")
			.field("step", step)
			.field("status", status)
			.submit();
	}

	operator_logger.warn("rollout still draining old connections");
	operator_logger.error("database connection pool is degraded");

	return 0;
}