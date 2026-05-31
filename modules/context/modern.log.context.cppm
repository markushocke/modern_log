module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

export module modern.log.context;

export import modern.log.core;

import modern.runtime;
import modern.trace;

namespace modern::log::detail {

[[nodiscard]] inline std::uint64_t current_thread_id_context() {
	return static_cast<std::uint64_t>(
		std::hash<std::thread::id>{}(std::this_thread::get_id())
	);
}

[[nodiscard]] inline std::uint64_t pointer_fingerprint(const void* value) noexcept {
	return static_cast<std::uint64_t>(std::hash<const void*>{}(value));
}

} // namespace modern::log::detail

export namespace modern::log {

struct context_snapshot final {
	std::uint64_t thread_id{};
	std::uint64_t task_id{};
	trace_context_handle trace_context{};
	std::string trace_id{};
	std::string span_id{};
	std::string traceparent{};
	std::shared_ptr<const void> trace_context_owner{};
};

class context_provider {
public:
	virtual ~context_provider() = default;

	[[nodiscard]] virtual context_snapshot capture() noexcept = 0;
};

namespace detail {

inline context_provider*& context_provider_storage() noexcept {
	thread_local context_provider* provider = nullptr;
	return provider;
}

class default_context_provider final : public context_provider {
public:
	[[nodiscard]] context_snapshot capture() noexcept override {
		context_snapshot snapshot{};
		snapshot.thread_id = current_thread_id_context();

		const auto environment = modern::runtime::current_task_environment_value();

		if (environment.scheduler != nullptr) {
			snapshot.task_id = pointer_fingerprint(environment.scheduler);
		}

		if (environment.trace_context && environment.trace_context->is_valid()) {
			auto trace = std::make_shared<modern::trace::TraceContext>(*environment.trace_context);
			snapshot.trace_context_owner = trace;
			snapshot.trace_context.native_context = trace.get();
			snapshot.traceparent = modern::trace::format_traceparent(*trace);

			if (snapshot.traceparent.size() >= 55) {
				snapshot.trace_id = snapshot.traceparent.substr(3, 32);
				snapshot.span_id = snapshot.traceparent.substr(36, 16);
			}

			if (snapshot.task_id == 0) {
				snapshot.task_id = pointer_fingerprint(trace.get());
			}
		}

		return snapshot;
	}
};

inline default_context_provider& default_provider() noexcept {
	static default_context_provider provider;
	return provider;
}

} // namespace detail

[[nodiscard]] inline context_snapshot capture_context() noexcept {
	if (auto* provider = detail::context_provider_storage()) {
		return provider->capture();
	}

	return detail::default_provider().capture();
}

inline context_provider* set_context_provider(context_provider* provider) noexcept {
	auto*& current = detail::context_provider_storage();
	auto* previous = current;
	current = provider;
	return previous;
}

class scoped_context_provider final {
public:
	explicit scoped_context_provider(context_provider* provider) noexcept
		: previous_(set_context_provider(provider)) {}

	scoped_context_provider(const scoped_context_provider&) = delete;
	scoped_context_provider& operator=(const scoped_context_provider&) = delete;

	~scoped_context_provider() {
		set_context_provider(previous_);
	}

private:
	context_provider* previous_{};
};

} // namespace modern::log