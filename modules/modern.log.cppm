module;

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "./detail/field_arena.hpp"

export module modern.log;

export import modern.log.core;
export import modern.log.async;
export import modern.log.sinks;
export import modern.log.format;
export import modern.log.context;
export import modern.log.metrics;

namespace modern::log::detail {

[[nodiscard]] inline std::uint64_t current_timestamp_ns() {
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()
	);
}

[[nodiscard]] inline std::uint64_t current_thread_id() {
	return static_cast<std::uint64_t>(
		std::hash<std::thread::id>{}(std::this_thread::get_id())
	);
}

} // namespace modern::log::detail

export namespace modern::log {

class logger_builder;
class event_builder;

class logger {
public:
	[[nodiscard]] static logger_builder builder();

	[[nodiscard]] logger category(std::string_view category_name) const {
		return logger{sinks_, std::string(category_name)};
	}

	[[nodiscard]] event_builder event(std::string_view event_name) const;

	void trace(std::string_view message) const {
		submit(level::trace, message);
	}

	void debug(std::string_view message) const {
		submit(level::debug, message);
	}

	void info(std::string_view message) const {
		submit(level::info, message);
	}

	void warn(std::string_view message) const {
		submit(level::warn, message);
	}

	void error(std::string_view message) const {
		submit(level::error, message);
	}

	void fatal(std::string_view message) const {
		submit(level::fatal, message);
	}

private:
	friend class logger_builder;

	explicit logger(
		std::shared_ptr<std::vector<std::shared_ptr<sink>>> sinks,
		std::string category = {}
	)
		: sinks_(std::move(sinks)),
		  category_(std::move(category)) {}

	void submit(level message_level, std::string_view message) const {
		if (!sinks_ || sinks_->empty()) {
			return;
		}

		const auto context = capture_context();

		record entry{};
		entry.timestamp_ns = detail::current_timestamp_ns();
		entry.log_level = message_level;
		entry.category = category_;
		entry.thread_id = context.thread_id != 0 ? context.thread_id : detail::current_thread_id();
		entry.task_id = context.task_id;
		entry.trace_context = context.trace_context;
		entry.trace_id = context.trace_id;
		entry.span_id = context.span_id;
		entry.traceparent = context.traceparent;
		entry.message_template = message;

		write(entry);
	}

	void write(record& entry) const {
		const batch_view batch{&entry, 1};
		for (const auto& target : *sinks_) {
			if (target) {
				dispatch_async_write(*target, async_sink_write{batch, true});
			}
		}
	}

	std::shared_ptr<std::vector<std::shared_ptr<sink>>> sinks_{};
	std::string category_{};
};

class event_builder {
public:
	event_builder(
		std::shared_ptr<std::vector<std::shared_ptr<sink>>> sinks,
		std::string category,
		std::string event_name
	)
		: sinks_(std::move(sinks)),
		  category_(std::move(category)),
		  event_name_(std::move(event_name)),
		  string_storage_(field_arena_.resource()),
		  fields_(field_arena_.resource()) {
		fields_.reserve(4);
	}

	template <typename Integer>
	requires (std::is_integral_v<Integer> && !std::is_same_v<std::remove_cv_t<Integer>, bool>)
	event_builder& field(std::string_view name, Integer value) {
		if constexpr (std::is_signed_v<Integer>) {
			return add_signed_field(name, static_cast<std::int64_t>(value));
		}

		return add_unsigned_field(name, static_cast<std::uint64_t>(value));
	}

	template <typename Float>
	requires std::is_floating_point_v<Float>
	event_builder& field(std::string_view name, Float value) {
		return add_floating_field(name, static_cast<double>(value));
	}

	event_builder& field(std::string_view name, bool value) {
		modern::log::field entry{};
		entry.name = store_string(name);
		entry.value.type = field_type::boolean;
		entry.value.storage.boolean = value;
		fields_.push_back(entry);
		return *this;
	}

	event_builder& field(std::string_view name, std::string_view value) {
		modern::log::field entry{};
		entry.name = store_string(name);
		entry.value.type = field_type::string;
		entry.value.storage.string = store_string(value);
		fields_.push_back(entry);
		return *this;
	}

	event_builder& field(std::string_view name, const char* value) {
		return field(name, std::string_view{value != nullptr ? value : ""});
	}

	void submit() const {
		if (!sinks_ || sinks_->empty()) {
			return;
		}

		const auto context = capture_context();

		record entry{};
		entry.timestamp_ns = detail::current_timestamp_ns();
		entry.log_level = level::info;
		entry.category = category_;
		entry.event_name = event_name_;
		entry.thread_id = context.thread_id != 0 ? context.thread_id : detail::current_thread_id();
		entry.task_id = context.task_id;
		entry.trace_context = context.trace_context;
		entry.trace_id = context.trace_id;
		entry.span_id = context.span_id;
		entry.traceparent = context.traceparent;
		entry.fields = std::span<const modern::log::field>(fields_.data(), fields_.size());

		const batch_view batch{&entry, 1};
		for (const auto& target : *sinks_) {
			if (target) {
				dispatch_async_write(*target, async_sink_write{batch, true});
			}
		}
	}

private:
	event_builder& add_signed_field(std::string_view name, std::int64_t value) {
		modern::log::field entry{};
		entry.name = store_string(name);
		entry.value.type = field_type::signed_integer;
		entry.value.storage.signed_integer = value;
		fields_.push_back(entry);
		return *this;
	}

	event_builder& add_unsigned_field(std::string_view name, std::uint64_t value) {
		modern::log::field entry{};
		entry.name = store_string(name);
		entry.value.type = field_type::unsigned_integer;
		entry.value.storage.unsigned_integer = value;
		fields_.push_back(entry);
		return *this;
	}

	event_builder& add_floating_field(std::string_view name, double value) {
		modern::log::field entry{};
		entry.name = store_string(name);
		entry.value.type = field_type::floating_point;
		entry.value.storage.floating_point = value;
		fields_.push_back(entry);
		return *this;
	}

	[[nodiscard]] std::string_view store_string(std::string_view value) {
		string_storage_.emplace_back(value);
		return string_storage_.back();
	}

	std::shared_ptr<std::vector<std::shared_ptr<sink>>> sinks_{};
	std::string category_{};
	std::string event_name_{};
	detail::field_arena<1024> field_arena_{};
	std::deque<std::pmr::string, std::pmr::polymorphic_allocator<std::pmr::string>> string_storage_;
	std::pmr::vector<modern::log::field> fields_;
};

class logger_builder {
public:
	logger_builder& sink(std::shared_ptr<modern::log::sink> sink_target) {
		if (sink_target) {
			sinks_.push_back(std::move(sink_target));
		}
		return *this;
	}

	logger_builder& sink(modern::log::sink& sink_target) {
		sinks_.push_back(
			std::shared_ptr<modern::log::sink>(&sink_target, [](modern::log::sink*) {})
		);
		return *this;
	}

	[[nodiscard]] logger build() const {
		return logger{std::make_shared<std::vector<std::shared_ptr<modern::log::sink>>>(sinks_)};
	}

private:
	std::vector<std::shared_ptr<modern::log::sink>> sinks_{};
};

inline logger_builder logger::builder() {
	return logger_builder{};
}

inline event_builder logger::event(std::string_view event_name) const {
	return event_builder{sinks_, category_, std::string(event_name)};
}

} // namespace modern::log