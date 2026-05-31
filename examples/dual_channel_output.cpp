import modern.log;

#include <memory>

int main() {
	using namespace modern::log;

	auto machine_logger = logger::builder()
		.sink(std::make_shared<json_sink>())
		.build();

	auto operator_logger = logger::builder()
		.sink(std::make_shared<stderr_sink>())
		.build();

	machine_logger.event("deployment.started")
		.field("service", "catalog")
		.field("revision", "2026.05.30")
		.submit();

	operator_logger.error("database connection pool is degraded");

	return 0;
}