# modern-log

Structured, trace-aware, runtime-native logging for C++23 modules.

modern-log is a structured logging library for systems that already think in
records, traces, schedulers, queues, and operational telemetry, not just in
plain strings.

It combines:

- structured records and event fields
- synchronous and asynchronous logger APIs
- ISO-8601 UTC timestamps plus raw nanosecond timestamps in text and JSON output
- direct modern_trace integration for trace_id, span_id, and traceparent
- direct modern_runtime integration for scheduler-aware async draining
- modern_io-backed file and rotating file output
- simple sink extensibility for text, JSON, file, rotating file, console, stderr, remote, OpenTelemetry bridge, and custom sinks

The result is a logger that fits applications that already care about tracing,
runtime scheduling, batching, and operationally useful telemetry.

## At A Glance

| If you need... | modern-log gives you... |
| --- | --- |
| logs as data, not just text | categories, event names, typed fields, JSON formatting |
| trace-aware diagnostics | trace_id, span_id, and traceparent propagation from modern_trace |
| app-owned trace enrichment without a second API | `context_provider` and `scoped_context_provider` instead of logger-local trace mutators |
| hot-path timestamp output | cached UTC second prefixes for readable timestamps on bursty log streams |
| lower formatter churn | reusable internal append buffers across repeated record and batch formatting |
| lower field-capture churn | arena-backed PMR staging for field names and string values before dispatch or enqueue |
| reproducible tuning data | built-in hot-path benchmark target for formatter throughput and async enqueue latency |
| readable plus high-detail time | ISO-8601 UTC timestamp strings plus raw nanosecond timestamps |
| async logging with control | queue capacity, batch limit, wakeup threshold, flush interval, backpressure policy |
| runtime-native behavior | async draining on modern_runtime schedulers |
| bounded local retention | built-in rotating_file_sink with size-based rollover and archive retention |
| application-owned remote shipping | built-in remote_sink over a transport seam with JSON or text payloads |
| OpenTelemetry export without SDK lock-in | built-in otel_sink over an application-owned exporter seam |
| multiple output shapes | console, stderr, JSON, file, rotating file, remote, OpenTelemetry bridge, and custom sinks |
| testability and extension | tiny sink seam and explicit context provider seam |

### Example Gallery

- [examples/structured_asset_pipeline.cpp](examples/structured_asset_pipeline.cpp): sync structured bursts for asset or ETL pipelines
- [examples/async_runtime_trace.cpp](examples/async_runtime_trace.cpp): async runtime worker logging with trace propagation
- [examples/dual_channel_output.cpp](examples/dual_channel_output.cpp): burst-oriented machine-readable JSON plus human stderr output
- [examples/custom_context_injection.cpp](examples/custom_context_injection.cpp): application-owned request, task, and trace context injection without extra tracing mutators
- [examples/rotating_file_sink.cpp](examples/rotating_file_sink.cpp): size-based file rollover with bounded archive retention
- [examples/remote_sink_bridge.cpp](examples/remote_sink_bridge.cpp): application-owned remote transport bridge with built-in payload formatting
- [examples/otel_sink_bridge.cpp](examples/otel_sink_bridge.cpp): application-owned OpenTelemetry exporter bridge without SDK lock-in

## What Is modern-log?

modern-log is a runtime-native logging pipeline with a small public surface:

- `import modern.log;`
- `modern::log::logger` for synchronous logging
- `modern::log::async_logger` for batched asynchronous logging
- `modern::log::sink` as the extension seam for output targets
- structured events with typed fields
- context capture for thread, task, and trace information

Current implemented scope includes:

- text formatting
- JSON formatting
- human-readable UTC timestamps alongside raw nanosecond timestamps
- cached UTC second-prefix formatting on hot burst paths
- reusable internal append buffers for repeated formatter calls
- arena-backed PMR staging for sync and async field capture
- reusable async drain workspace for batch, record, and field materialization
- cache-line separation for async coordination flags and metrics
- console, stderr, JSON, file, rotating file, remote, and otel sinks
- async drain batching and explicit flush/shutdown lifecycle
- runtime scheduler integration
- trace propagation from modern_trace and modern_runtime
- queue pressure metrics on the async path
- hot-path benchmark target for formatter throughput and async enqueue latency

This repository is not modeled around a process-global singleton logger. You build
loggers explicitly and inject the sinks you want.

## Why modern-log?

### Structured first, not string-first

Most logging APIs make it easy to emit text and hard to emit data. modern-log
starts with records, event names, categories, fields, and trace metadata so
downstream systems can work with logs as data.

### Async without giving up control

The async path supports queue sizing, batch sizing, wakeup thresholds, flush
intervals, and backpressure policy selection. That gives you predictable behavior
under load instead of hidden blocking.

### Trace-aware by default

modern-log consumes the trace contract from modern_trace instead of inventing its
own tracing model. When trace context exists, records carry trace_id, span_id,
and traceparent all the way to the sink.

### Native fit for runtime-driven systems

If your system already runs on modern_runtime, the async worker can use the
runtime scheduler directly. That keeps logging aligned with the rest of the
execution model instead of creating a second unrelated async world.

### Easy to extend

The sink contract is intentionally small. If you want to ship logs to a ring
buffer, an in-memory test collector, a sidecar process, or a custom transport,
you implement one `write(batch_view)` method and optionally `flush()`.

## How To Build

Recommended toolchain:

- CMake 3.28+
- Clang 18+
- Ninja

Dependency resolution is automatic and always follows this order:

1. checkout under the active build directory at `build*/deps/modern_trace`, `build*/deps/modern_runtime`, `build*/deps/modern_io`
2. sibling checkout next to this repo at `../modern_trace`, `../modern_runtime`, `../modern_io`
3. installed artifact via `find_package(...)`

That means the common local workflow is just sibling checkouts plus a normal
configure/build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure -R '^modern_log\.'
```

The filtered `ctest` command is the right validation command for this repo,
because vendored dependency repos can register their own upstream tests when they
are available as local checkouts.

To build the example gallery only:

```bash
cmake --build build --target modern_log_examples
```

## How To Use

### Add modern-log to a CMake project

If you vendor the repo directly:

```cmake
add_subdirectory(external/modern_log)

target_link_libraries(my_app
  PRIVATE
    modern_log::modern_log
)
```

Then in code:

```cpp
import modern.log;
```

### Smallest useful logger

Buildable example: [examples/dual_channel_output.cpp](examples/dual_channel_output.cpp)

```cpp
import modern.log;

#include <memory>

int main() {
	using namespace modern::log;

	auto logger = logger::builder()
		.sink(std::make_shared<console_sink>())
		.build();

	logger.info("runtime started");
	logger.category("render").warn("shader fallback");
}
```

Text output looks like this shape:

```text
timestamp=2026-05-31T10:15:30.123456789Z ts=1780222530123456789 level=info message="runtime started"
timestamp=2026-05-31T10:15:30.123456889Z ts=1780222530123456889 level=warn category=render message="shader fallback"
```

## Cool Use Cases

### 1. Structured asset pipeline events

When you care about analysis later, emit event names and typed fields instead of
packing everything into a single sentence. This example also matches the common
asset-pipeline burst shape where many similarly structured records are formatted
back-to-back.

Buildable example: [examples/structured_asset_pipeline.cpp](examples/structured_asset_pipeline.cpp)

```cpp
import modern.log;

#include <array>
#include <memory>
#include <string_view>

struct asset_event final {
	std::string_view path;
	std::uint64_t bytes;
	double duration_ms;
	bool cached;
};

int main() {
	constexpr std::array assets{
		asset_event{"ship.mesh", 65536, 3.5, true},
		asset_event{"pilot.anim", 12288, 1.4, false},
		asset_event{"terrain.bin", 524288, 8.9, true},
	};

	auto logger = modern::log::logger::builder()
		.sink(std::make_shared<modern::log::json_sink>())
		.build();

	for (const auto& asset : assets) {
		logger.category("assets")
			.event("asset.loaded")
			.field("path", asset.path)
			.field("bytes", asset.bytes)
			.field("duration_ms", asset.duration_ms)
			.field("cached", asset.cached)
			.submit();
	}
}
```

Example JSON line:

```json
{"timestamp":"2026-05-31T10:15:30.123456789Z","timestamp_ns":1780222530123456789,"level":"info","category":"assets","event":"asset.loaded","path":"ship.mesh","bytes":65536,"duration_ms":3.5,"cached":true}
```

Use this shape for:

- asset pipelines
- API audit events
- ETL/import jobs
- background task telemetry

### 2. Async runtime worker logs with trace propagation

This is the high-value path for services and worker systems that already use
modern_runtime and modern_trace.

Buildable example: [examples/async_runtime_trace.cpp](examples/async_runtime_trace.cpp)

```cpp
import modern.log;
import modern.runtime;
import modern.trace;

#include <chrono>
#include <filesystem>
#include <memory>

using namespace std::chrono_literals;

int main() {
	modern::trace::TraceContext trace{};
	trace.trace_id[15] = std::byte{0x47};
	trace.span_id[7] = std::byte{0xb7};

	modern::thread_pool pool{2};
	auto logger = modern::log::async_logger::builder()
		.sink(std::make_shared<modern::log::json_sink>())
		.sink(std::make_shared<modern::log::file_sink>(std::filesystem::path{"runtime.log"}))
		.batch_limit(8)
		.wakeup_threshold(8)
		.flush_interval(10ms)
		.scheduler(pool.get_scheduler())
		.build();

	{
		modern::runtime::TraceContextScope scope{trace};
		logger.category("runtime").info("runtime scheduled");
	}

	logger.flush();
	logger.shutdown();
	pool.shutdown();
	pool.join();
}
```

What you get at the sink:

- async draining instead of producer-thread writes
- runtime scheduler integration
- trace_id, span_id, and traceparent on each record
- deterministic flush/shutdown lifecycle

### 3. JSON to stdout, text to stderr

This is useful for service processes where JSON is collected by infrastructure
while operators still want readable error output on the terminal. It also shows
the common burst pattern where multiple records land in the same UTC second and
benefit from cached readable timestamp prefixes.

```cpp
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
}
```

Use this when you want:

- machine-readable logs for ingestion
- operator-readable errors in stderr
- zero extra adapters in early deployment stages

### 4. Custom request or task context injection

Sometimes the runtime has only part of the metadata and your application owns the
rest. modern-log exposes a context provider seam for exactly that. This is also
the supported way to inject application-owned trace metadata; modern-log does
not add `trace_id(...)` or `traceparent(...)` mutators on logger or event
builders because modern_trace owns those primitives.

Buildable example: [examples/custom_context_injection.cpp](examples/custom_context_injection.cpp)

```cpp
import modern.log;

#include <cstdint>
#include <memory>
#include <string>

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
	logger.event("request.completed")
		.field("status_code", std::uint64_t{200})
		.submit();
}
```

This is useful for:

- gateway/request correlation
- embedding framework-owned request IDs
- carrying application-owned trace metadata without duplicating tracing helpers
- testing context propagation deterministically

### 5. Backpressure with measurable behavior

Async logging should tell you when overload happens. The async logger exposes both
queue controls and metrics.

```cpp
import modern.log;

#include <memory>

int main() {
	auto logger = modern::log::async_logger::builder()
		.sink(std::make_shared<modern::log::stderr_sink>())
		.queue_capacity(1024)
		.batch_limit(64)
		.wakeup_threshold(16)
		.backpressure_policy(modern::log::backpressure_policy::drop_newest)
		.build();

	for (int index = 0; index != 5000; ++index) {
		logger.category("ingest").info("frame received");
	}

	logger.flush();

	const auto metrics = logger.metrics();
	if (metrics.dropped_records != 0) {
		// export metrics, trigger alerting, or adjust queue policy
	}

	logger.shutdown();
}
```

Current metrics surface:

- enqueue_attempts
- enqueued_records
- dropped_records
- enqueue_failures
- current_queue_depth
- high_water_mark
- written_records
- sink_write_calls
- sink_flush_calls
- flush_count
- write_failures (counts sink write and sink flush exceptions)

### 6. In-memory sink for tests, replay, or adapters

The sink API is intentionally tiny. That makes it easy to build test collectors
or bridge sinks.

```cpp
import modern.log;

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct captured_record final {
	std::string category;
	std::string event_name;
	std::string message;
};

class collecting_sink final : public modern::log::sink {
public:
	void write(modern::log::batch_view batch) override {
		for (std::size_t index = 0; index < batch.size; ++index) {
			const auto& current = batch.data[index];
			records.push_back(captured_record{
				std::string(current.category),
				std::string(current.event_name),
				std::string(current.message_template),
			});
		}
	}

	std::vector<captured_record> records{};
};

int main() {
	auto sink = std::make_shared<collecting_sink>();
	auto logger = modern::log::logger::builder()
		.sink(sink)
		.build();

	logger.category("orders").info("order accepted");
	logger.event("order.shipped").field("order_id", std::uint64_t{42}).submit();
}
```

This pattern is useful for:

- unit and integration tests
- forwarding into metrics/adapters
- buffering logs before shipping them elsewhere

### 7. Bounded local file retention

Some services need local log files for support bundles or sidecar pickup, but
they still need predictable disk growth. `rotating_file_sink` keeps the active
file open, rolls it when the next write would exceed the byte budget, and keeps
only a bounded number of archived files.

Buildable example: [examples/rotating_file_sink.cpp](examples/rotating_file_sink.cpp)

```cpp
import modern.log;

#include <cstdint>
#include <filesystem>
#include <memory>

int main() {
	const auto path = std::filesystem::current_path() / "service.log";
	auto sink = std::make_shared<modern::log::rotating_file_sink>(
		path,
		std::uintmax_t{4096},
		3
	);

	auto logger = modern::log::logger::builder()
		.sink(sink)
		.build();

	logger.category("http").info("listener ready");
	logger.event("request.completed")
		.field("status_code", std::uint64_t{200})
		.field("bytes_written", std::uint64_t{512})
		.submit();
}
```

Use this shape for:

- services that need local support bundles without unbounded disk growth
- agents or sidecars that tail one active file plus a small archive window
- deployments where file rotation policy belongs inside the application process

### 8. Application-owned remote shipping bridge

Some teams want a built-in remote sink shape without hard-wiring HTTP, gRPC, or
another transport into the logger core. `remote_sink` keeps formatting in
modern-log while the application owns the actual transport.

Buildable example: [examples/remote_sink_bridge.cpp](examples/remote_sink_bridge.cpp)

```cpp
import modern.log;

#include <iostream>
#include <memory>
#include <string>

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
}
```

Use this shape for:

- webhook or HTTP ingestion bridges
- gRPC or message-bus relays owned by the application
- sidecar handoff where the transport library should stay outside the logger core

### 9. Application-owned OpenTelemetry exporter bridge

Some teams want OpenTelemetry log export, but they do not want the logger core
to own SDK setup, processor lifetimes, or exporter transport policy. `otel_sink`
maps modern-log records into OTel-shaped views and hands them to an
application-owned exporter.

Buildable example: [examples/otel_sink_bridge.cpp](examples/otel_sink_bridge.cpp)

```cpp
import modern.log;

#include <cstdint>
#include <iostream>
#include <memory>
#include <span>

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
			std::cout << '\n';
		}
	}
};

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
}
```

Use this shape for:

- applications that already own an OpenTelemetry SDK provider and exporter policy
- bridges into batch or OTLP exporters that should stay outside the logger core
- keeping severity, body, attributes, and trace context mapping centralized in one sink

## Design Notes

- modern_trace owns the trace contract, not this repo
- modern_runtime owns scheduler and task-environment behavior, not this repo
- modern_io owns the file output primitive used by file_sink and rotating_file_sink
- modern-log owns the OTel bridge record mapping, not an OpenTelemetry SDK dependency
- modern-log keeps application-owned trace enrichment behind `context_provider` instead of adding a second direct tracing API
- modern-log owns record shape, formatting, sink orchestration, async queueing, and lifecycle behavior

## Operational Notes

- Prefer `logger` for direct and synchronous local output.
- Prefer `async_logger` for services, workers, pipelines, and anything that should keep producer threads light.
- Call `flush()` when you need a synchronization point.
- Call `shutdown()` before process teardown on the async path.
- Text and JSON formatters cache the UTC second prefix, so bursty logs keep readable timestamps without rebuilding the whole calendar string every time.
- Text and JSON formatters also reuse their internal append buffers across repeated calls, which reduces formatter-side allocation churn on record and batch hot paths.
- Sync and async event builders stage field names and string values inside a monotonic arena before dispatch or enqueue, which removes per-field heap churn from the capture path.
- `rotating_file_sink` rotates whole writes and keeps up to `max_archives` files beside the active path; set `max_archives` to `0` when you want rollover without retaining archives.
- `remote_sink` defaults to newline-delimited JSON payloads; switch to `remote_payload_format::text` only when the downstream transport expects rendered text lines.
- `otel_sink` forwards non-owning record and field views during `write()`; exporters must copy anything they want to retain after the export call returns.
- If the application owns trace metadata, inject it through `context_provider` or `scoped_context_provider` rather than adding logger-local trace setters.
- The async worker reuses its drain-side batch, record, and field scratch space across iterations, so steady workloads stop rebuilding those vectors from scratch on every drain.
- Async coordination flags and metrics now live on separate cache-line-sized blocks, which reduces avoidable cross-thread cache bouncing on producer versus worker state.
- If enqueue tail latency matters more than sink call count, keep `flush_interval(0ms)` on sustained hot paths and raise `wakeup_threshold` only gradually.
- If sink overhead dominates, favor larger `batch_limit` plus `wakeup_threshold` and a periodic flush; the benchmark target makes that trade-off measurable instead of guessed.
- Use categories and event names consistently; that is what makes logs queryable.

## Performance Benchmarks

Buildable benchmark target: [benchmarks/hot_path_benchmark.cpp](benchmarks/hot_path_benchmark.cpp)

Run it with:

```bash
cmake --build build --target modern_log_hot_path_benchmark
./build/benchmarks/modern_log_hot_path_benchmark --records 120000
```

The benchmark measures:

- text formatter throughput
- JSON formatter throughput
- async enqueue throughput and p50/p95 enqueue latency
- sink write-call coalescing under different queue and batching profiles

Recent local sample on this Linux workspace with `--records 120000`:

- text formatter: about 271946 records/s
- JSON formatter: about 96592 records/s
- async baseline (`queue_capacity=2048`, `batch_limit=64`, `wakeup_threshold=1`, `flush_interval=0ms`): about 160205 records/s, enqueue p50 5765 ns, enqueue p95 8208 ns, 119213 sink write calls
- async batch-heavy profile (`queue_capacity=8192`, `batch_limit=256`, `wakeup_threshold=128`, `flush_interval=10ms`): about 160278 records/s, enqueue p50 5420 ns, enqueue p95 8438 ns, 2559 sink write calls

Measured takeaway from that run:

- the batch-heavy profile reduced sink write calls by about 97.85%
- in this run throughput stayed effectively flat at about +0.05%, while enqueue p95 rose by about 2.80%
- use that profile when sink or I/O cost dominates; keep `flush_interval(0ms)` if your primary requirement is the lowest enqueue tail latency

## Current Validation Baseline

The current repo-owned validation command is:

```bash
ctest --test-dir build --output-on-failure -R '^modern_log\.'
```

At the current state this covers:

- core contracts
- queue contracts
- sinks and formatters
- context capture
- sync logger API
- async logger lifecycle
- async drain semantics
- JSON sinks
- real modern_runtime plus modern_trace plus modern_io integration