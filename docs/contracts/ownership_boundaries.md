# Ownership Boundaries

modern_trace owns:
- TraceContext and shared correlation primitives
- traceparent parsing and formatting helpers
- generic trace drain and fan-in primitives

modern_runtime owns:
- scheduler integration
- timer integration
- worker and task execution context

modern_io owns:
- file and async I/O primitives used by sinks
- transport-facing write operations

modern_log owns:
- log record schema and batch views
- queue state and batching behavior
- backpressure policy selection and accounting
- formatter and sink orchestration
- logger-facing adapters that consume modern_trace and modern_runtime

Rules:
- modern_log consumes shared trace primitives and does not redefine them.
- runtime-specific churn must stay behind internal adapter seams.
- sink behavior may depend on modern_io, but modern_io must not depend on modern_log.