module;

#include <concepts>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../detail/queue_policy.hpp"

export module modern.log.async;

export import modern.log.core;
export import modern.log.sinks;
export import modern.log.metrics;

import modern.log.context;

import modern.trace;
import modern.runtime;

namespace modern::log::detail {

using trace_drain_mode = modern::trace::TraceDrainMode;
using trace_drain_state = modern::trace::TraceDrainState;
using trace_drain_step_result = modern::trace::TraceDrainStepResult;

[[nodiscard]] inline std::uint64_t current_timestamp_ns_async() {
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()
	);
}

[[nodiscard]] inline std::uint64_t current_thread_id_async() {
	return static_cast<std::uint64_t>(
		std::hash<std::thread::id>{}(std::this_thread::get_id())
	);
}

struct owned_field_value final {
	field_type type{field_type::string};
	std::int64_t signed_integer{};
	std::uint64_t unsigned_integer{};
	double floating_point{};
	bool boolean{};
	std::string string_storage{};
	std::vector<std::byte> bytes_storage{};
};

struct owned_field final {
	std::string name{};
	owned_field_value value{};
};

struct owned_record final {
	std::uint64_t timestamp_ns{};
	level log_level{level::info};
	std::string category{};
	std::string event_name{};
	std::uint64_t thread_id{};
	std::uint64_t task_id{};
	trace_context_handle trace_context{};
	std::shared_ptr<const void> trace_context_owner{};
	std::string trace_id{};
	std::string span_id{};
	std::string traceparent{};
	std::string message_template{};
	std::vector<owned_field> fields{};
	std::uint32_t dropped_count{};
};

class runtime_worker_adapter final {
public:
	runtime_worker_adapter() = default;

	explicit runtime_worker_adapter(modern::scheduler scheduler)
		: scheduler_(std::move(scheduler)) {}

	template <typename WorkerFn>
	void start(WorkerFn&& worker) {
		if (scheduler_.valid()) {
			std::promise<void> completion;
			completion_ = completion.get_future();

			using worker_type = std::decay_t<WorkerFn>;
			worker_type task(std::forward<WorkerFn>(worker));

			scheduler_.execute(
				[task = std::move(task), completion = std::move(completion)]() mutable {
					try {
						task();
						completion.set_value();
					} catch (...) {
						completion.set_exception(std::current_exception());
					}
				}
			);
			return;
		}

		worker_ = std::thread(std::forward<WorkerFn>(worker));
	}

	void join() {
		stop_periodic();

		if (completion_.valid()) {
			completion_.wait();
		}

		if (worker_.joinable()) {
			worker_.join();
		}
	}

	[[nodiscard]] bool uses_runtime_scheduler() const noexcept {
		return scheduler_.valid();
	}

	void restart_periodic(std::chrono::milliseconds interval, std::function<void()> callback) {
		if (!scheduler_.valid()) {
			return;
		}

		if (!periodic_) {
			periodic_ = std::make_unique<runtime_periodic_timer>(scheduler_);
		}

		periodic_->restart(interval, std::move(callback));
	}

	void stop_periodic() {
		if (periodic_) {
			periodic_->stop();
		}
	}

private:
	class runtime_periodic_timer final {
	public:
		explicit runtime_periodic_timer(modern::scheduler scheduler)
			: scheduler_(std::move(scheduler)) {}

		template <typename Callback>
		void restart(std::chrono::milliseconds interval, Callback&& callback) {
			stop();

			if (!scheduler_.valid() || interval.count() <= 0) {
				return;
			}

			executor_ = std::make_unique<modern::scheduled_executor>(scheduler_);
			handle_.emplace(
				executor_->schedule_fixed_rate(interval, interval, std::forward<Callback>(callback))
			);
		}

		void stop() {
			if (handle_) {
				handle_->request_stop();
				handle_.reset();
			}

			if (executor_) {
				executor_->shutdown();
				executor_->join();
				executor_.reset();
			}
		}

		~runtime_periodic_timer() {
			stop();
		}

	private:
		modern::scheduler scheduler_{};
		std::unique_ptr<modern::scheduled_executor> executor_{};
		std::optional<modern::scheduled_executor::periodic_handle> handle_{};
	};

	modern::scheduler scheduler_{};
	std::future<void> completion_{};
	std::unique_ptr<runtime_periodic_timer> periodic_{};

	std::thread worker_{};
};

class async_state;

[[nodiscard]] inline bool is_debug_level(level value) {
	return value == level::trace || value == level::debug;
}

[[nodiscard]] inline bool is_error_level(level value) {
	return value == level::error || value == level::fatal;
}

class async_state final {
public:
	async_state(
		std::shared_ptr<std::vector<std::shared_ptr<sink>>> sinks,
		queue_contract contract,
		lifecycle_contract lifecycle,
		backpressure_policy policy,
		std::chrono::milliseconds flush_interval,
		modern::scheduler runtime_scheduler
	)
		: sinks_(std::move(sinks)),
		  queue_contract_(contract),
		  lifecycle_contract_(lifecycle),
		  policy_(policy),
		  flush_interval_(flush_interval)
		  , worker_(std::move(runtime_scheduler)) {
		worker_.start([this] { worker_loop(); });
		configure_periodic_flush();
	}

	~async_state() {
		shutdown();
	}

	[[nodiscard]] bool enqueue(owned_record record) {
		std::unique_lock lock(mutex_);
		++metrics_.enqueue_attempts;

		while (queue_.size() >= queue_contract_.capacity_records) {
			if (shutdown_requested_) {
				++metrics_.enqueue_failures;
				return false;
			}

			switch (policy_) {
			case backpressure_policy::drop_newest:
				++metrics_.dropped_records;
				++metrics_.enqueue_failures;
				return false;
			case backpressure_policy::drop_oldest:
				queue_.pop_front();
				++metrics_.dropped_records;
				break;
			case backpressure_policy::block_producer:
				idle_cv_.wait(lock, [this] {
					return shutdown_requested_ || queue_.size() < queue_contract_.capacity_records;
				});
				continue;
			case backpressure_policy::sample_debug_logs:
				if (is_debug_level(record.log_level)) {
					++metrics_.dropped_records;
					++metrics_.enqueue_failures;
					return false;
				}

				if (drop_first_if([](const owned_record& current) {
					return is_debug_level(current.log_level);
				})) {
					++metrics_.dropped_records;
					break;
				}

				++metrics_.dropped_records;
				++metrics_.enqueue_failures;
				return false;
			case backpressure_policy::priority_preserve_errors:
				if (!is_error_level(record.log_level)) {
					++metrics_.dropped_records;
					++metrics_.enqueue_failures;
					return false;
				}

				if (drop_first_if([](const owned_record& current) {
					return !is_error_level(current.log_level);
				})) {
					++metrics_.dropped_records;
					break;
				}

				queue_.pop_front();
				++metrics_.dropped_records;
				break;
			}
		}

		queue_.push_back(std::move(record));
		++metrics_.enqueued_records;
		metrics_.high_water_mark = std::max(metrics_.high_water_mark, static_cast<std::uint64_t>(queue_.size()));

		lock.unlock();
		work_cv_.notify_one();
		return true;
	}

	void set_flush_interval(std::chrono::milliseconds interval) {
		{
			std::lock_guard lock(mutex_);
			flush_interval_ = interval;
		}

		configure_periodic_flush();
		work_cv_.notify_one();
	}

	void flush() {
		std::unique_lock lock(mutex_);
		flush_requested_ = true;
		drain_state_ = trace_drain_state::Requested;
		work_cv_.notify_one();

		idle_cv_.wait(lock, [this] {
			return queue_.empty() && !draining_ && drain_state_ == trace_drain_state::Idle;
		});

		++metrics_.flush_count;
	}

	void shutdown() {
		{
			std::lock_guard lock(mutex_);
			if (shutdown_complete_) {
				return;
			}

			shutdown_requested_ = true;
			flush_requested_ = true;
			drain_state_ = trace_drain_state::Requested;
		}

		worker_.stop_periodic();
		work_cv_.notify_all();
		idle_cv_.notify_all();
		worker_.join();

		std::lock_guard lock(mutex_);
		shutdown_complete_ = true;
	}

	[[nodiscard]] async_metrics snapshot() const {
		std::lock_guard lock(mutex_);
		auto snapshot = metrics_;
		snapshot.current_queue_depth = static_cast<std::uint64_t>(queue_.size());
		return snapshot;
	}

private:
	void configure_periodic_flush() {
		const auto interval = [this] {
			std::lock_guard lock(mutex_);
			return flush_interval_;
		}();

		worker_.restart_periodic(interval, [this] { request_periodic_flush(); });
	}

	void request_periodic_flush() {
		bool notify = false;

		{
			std::lock_guard lock(mutex_);
			if (!shutdown_requested_ && !queue_.empty()) {
				flush_requested_ = true;
				drain_state_ = trace_drain_state::Requested;
				notify = true;
			}
		}

		if (notify) {
			work_cv_.notify_one();
		}
	}

	[[nodiscard]] trace_drain_step_result make_drain_step_result(
		std::size_t drained_records,
		bool has_more
	) const noexcept {
		return trace_drain_step_result{
			drained_records,
			has_more ? trace_drain_state::Requested : trace_drain_state::Idle,
		};
	}

	template <typename Predicate>
	bool drop_first_if(Predicate predicate) {
		const auto iterator = std::find_if(queue_.begin(), queue_.end(), predicate);
		if (iterator == queue_.end()) {
			return false;
		}

		queue_.erase(iterator);
		return true;
	}

	void worker_loop() {
		std::vector<owned_record> batch;

		for (;;) {
			batch.clear();
			bool flush_now = false;
			trace_drain_mode drain_mode = trace_drain_mode::Flush;

			{
				std::unique_lock lock(mutex_);
				work_cv_.wait(lock, [this] {
					return shutdown_requested_
						|| flush_requested_
						|| drain_state_ == trace_drain_state::Requested
						|| !queue_.empty();
				});

				if (queue_.empty() && shutdown_requested_) {
					break;
				}

				flush_now = flush_requested_;
				flush_requested_ = false;

				const bool should_wait_for_batch = !flush_now
					&& drain_state_ != trace_drain_state::Requested
					&& !shutdown_requested_
					&& !queue_.empty()
					&& flush_interval_.count() > 0
					&& queue_.size() < queue_contract_.wakeup_record_threshold;

				if (should_wait_for_batch) {
					work_cv_.wait_for(lock, flush_interval_, [this] {
						return shutdown_requested_
							|| flush_requested_
							|| queue_.size() >= queue_contract_.wakeup_record_threshold;
					});

					if (queue_.empty() && shutdown_requested_) {
						break;
					}

					if (flush_requested_) {
						flush_now = true;
						flush_requested_ = false;
					}
				}

				if (queue_.empty()) {
					continue;
				}

				draining_ = true;

				const auto limit = std::min(queue_contract_.batch_record_limit, queue_.size());
				batch.reserve(limit);

				for (std::size_t index = 0; index < limit; ++index) {
					batch.push_back(std::move(queue_.front()));
					queue_.pop_front();
				}

				drain_mode = (flush_now || shutdown_requested_ || queue_.empty())
					? trace_drain_mode::Flush
					: trace_drain_mode::NoFlush;
			}

			auto step = drain(batch, drain_mode);

			{
				std::lock_guard lock(mutex_);
				metrics_.written_records += static_cast<std::uint64_t>(step.drained_records);
				draining_ = false;
				drain_state_ = queue_.empty()
					? trace_drain_state::Idle
					: trace_drain_state::Requested;
				step.next_state = drain_state_;
			}

			idle_cv_.notify_all();

			if (step.should_reschedule()) {
				work_cv_.notify_one();
			}
		}

		{
			std::lock_guard lock(mutex_);
			draining_ = false;
			drain_state_ = trace_drain_state::Idle;
		}

		idle_cv_.notify_all();
	}

	[[nodiscard]] trace_drain_step_result drain(
		const std::vector<owned_record>& batch,
		trace_drain_mode mode
	) {
		std::uint64_t sink_write_calls{};
		std::uint64_t sink_flush_calls{};
		std::uint64_t write_failures{};

		std::vector<std::vector<field>> field_batches;
		field_batches.reserve(batch.size());

		std::vector<record> records;
		records.reserve(batch.size());

		for (const auto& current : batch) {
			auto& fields = field_batches.emplace_back();
			fields.reserve(current.fields.size());

			for (const auto& source_field : current.fields) {
				field target{};
				target.name = source_field.name;
				target.value.type = source_field.value.type;

				switch (source_field.value.type) {
				case field_type::signed_integer:
					target.value.storage.signed_integer = source_field.value.signed_integer;
					break;
				case field_type::unsigned_integer:
					target.value.storage.unsigned_integer = source_field.value.unsigned_integer;
					break;
				case field_type::floating_point:
					target.value.storage.floating_point = source_field.value.floating_point;
					break;
				case field_type::boolean:
					target.value.storage.boolean = source_field.value.boolean;
					break;
				case field_type::string:
					target.value.storage.string = source_field.value.string_storage;
					break;
				case field_type::bytes:
					target.value.storage.bytes = byte_view{
						source_field.value.bytes_storage.data(),
						source_field.value.bytes_storage.size(),
					};
					break;
				}

				fields.push_back(target);
			}

			records.push_back(record{
				.timestamp_ns = current.timestamp_ns,
				.log_level = current.log_level,
				.category = current.category,
				.event_name = current.event_name,
				.thread_id = current.thread_id,
				.task_id = current.task_id,
				.trace_context = current.trace_context,
				.trace_id = current.trace_id,
				.span_id = current.span_id,
				.traceparent = current.traceparent,
				.message_template = current.message_template,
				.fields = std::span<const field>(fields.data(), fields.size()),
				.dropped_count = current.dropped_count,
			});
		}

		const batch_view write_batch{records.data(), records.size()};
		for (const auto& target : *sinks_) {
			if (!target) {
				continue;
			}

			try {
				++sink_write_calls;
				target->write(write_batch);

				if (mode == trace_drain_mode::Flush) {
					++sink_flush_calls;
					target->flush();
				}
			} catch (...) {
				++write_failures;
			}
		}

		if (sink_write_calls != 0 || sink_flush_calls != 0 || write_failures != 0) {
			std::lock_guard lock(mutex_);
			metrics_.sink_write_calls += sink_write_calls;
			metrics_.sink_flush_calls += sink_flush_calls;
			metrics_.write_failures += write_failures;
		}

		return make_drain_step_result(records.size(), false);
	}

	std::shared_ptr<std::vector<std::shared_ptr<sink>>> sinks_{};
	queue_contract queue_contract_{};
	lifecycle_contract lifecycle_contract_{};
	backpressure_policy policy_{backpressure_policy::drop_newest};
	std::chrono::milliseconds flush_interval_{0};
	mutable std::mutex mutex_{};
	std::condition_variable work_cv_{};
	std::condition_variable idle_cv_{};
	std::deque<owned_record> queue_{};
	async_metrics metrics_{};
	bool shutdown_requested_{};
	bool shutdown_complete_{};
	bool flush_requested_{};
	bool draining_{};
	trace_drain_state drain_state_{trace_drain_state::Idle};
	runtime_worker_adapter worker_{};
};

} // namespace modern::log::detail

export namespace modern::log {

using detail::backpressure_policy;

class async_logger_builder;
class async_event_builder;

class async_logger {
public:
	[[nodiscard]] static async_logger_builder builder();

	[[nodiscard]] async_logger category(std::string_view category_name) const {
		return async_logger{state_, std::string(category_name)};
	}

	[[nodiscard]] async_event_builder event(std::string_view event_name) const;

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

	template <typename Rep, typename Period>
	void flush_every(std::chrono::duration<Rep, Period> interval) const {
		if (state_) {
			state_->set_flush_interval(std::chrono::duration_cast<std::chrono::milliseconds>(interval));
		}
	}

	void flush() const {
		if (state_) {
			state_->flush();
		}
	}

	void shutdown() const {
		if (state_) {
			state_->shutdown();
		}
	}

	[[nodiscard]] async_metrics metrics() const {
		if (!state_) {
			return {};
		}

		const auto snapshot = state_->snapshot();
		return async_metrics{
			snapshot.enqueue_attempts,
			snapshot.enqueued_records,
			snapshot.dropped_records,
			snapshot.enqueue_failures,
			snapshot.current_queue_depth,
			snapshot.high_water_mark,
			snapshot.written_records,
			snapshot.sink_write_calls,
			snapshot.sink_flush_calls,
			snapshot.flush_count,
			snapshot.write_failures,
		};
	}

private:
	friend class async_logger_builder;

	explicit async_logger(
		std::shared_ptr<detail::async_state> state,
		std::string category = {}
	)
		: state_(std::move(state)),
		  category_(std::move(category)) {}

	void submit(level message_level, std::string_view message) const {
		if (!state_) {
			return;
		}

		const auto context = capture_context();

		detail::owned_record entry{};
		entry.timestamp_ns = detail::current_timestamp_ns_async();
		entry.log_level = message_level;
		entry.category = category_;
		entry.thread_id = context.thread_id != 0 ? context.thread_id : detail::current_thread_id_async();
		entry.task_id = context.task_id;
		entry.trace_context = context.trace_context;
		entry.trace_context_owner = context.trace_context_owner;
		entry.trace_id = context.trace_id;
		entry.span_id = context.span_id;
		entry.traceparent = context.traceparent;
		entry.message_template = std::string(message);

		[[maybe_unused]] const bool enqueued = state_->enqueue(std::move(entry));
	}

	std::shared_ptr<detail::async_state> state_{};
	std::string category_{};
};

class async_event_builder {
public:
	async_event_builder(
		std::shared_ptr<detail::async_state> state,
		std::string category,
		std::string event_name
	)
		: state_(std::move(state)),
		  category_(std::move(category)),
		  event_name_(std::move(event_name)) {}

	template <typename Integer>
	requires (std::is_integral_v<Integer> && !std::is_same_v<std::remove_cv_t<Integer>, bool>)
	async_event_builder& field(std::string_view name, Integer value) {
		if constexpr (std::is_signed_v<Integer>) {
			return add_signed_field(name, static_cast<std::int64_t>(value));
		}

		return add_unsigned_field(name, static_cast<std::uint64_t>(value));
	}

	template <typename Float>
	requires std::is_floating_point_v<Float>
	async_event_builder& field(std::string_view name, Float value) {
		return add_floating_field(name, static_cast<double>(value));
	}

	async_event_builder& field(std::string_view name, bool value) {
		detail::owned_field entry{};
		entry.name = std::string(name);
		entry.value.type = field_type::boolean;
		entry.value.boolean = value;
		fields_.push_back(std::move(entry));
		return *this;
	}

	async_event_builder& field(std::string_view name, std::string_view value) {
		detail::owned_field entry{};
		entry.name = std::string(name);
		entry.value.type = field_type::string;
		entry.value.string_storage = std::string(value);
		fields_.push_back(std::move(entry));
		return *this;
	}

	async_event_builder& field(std::string_view name, const char* value) {
		return field(name, std::string_view{value != nullptr ? value : ""});
	}

	void submit() const {
		if (!state_) {
			return;
		}

		const auto context = capture_context();

		detail::owned_record entry{};
		entry.timestamp_ns = detail::current_timestamp_ns_async();
		entry.log_level = level::info;
		entry.category = category_;
		entry.event_name = event_name_;
		entry.thread_id = context.thread_id != 0 ? context.thread_id : detail::current_thread_id_async();
		entry.task_id = context.task_id;
		entry.trace_context = context.trace_context;
		entry.trace_context_owner = context.trace_context_owner;
		entry.trace_id = context.trace_id;
		entry.span_id = context.span_id;
		entry.traceparent = context.traceparent;
		entry.fields = fields_;

		[[maybe_unused]] const bool enqueued = state_->enqueue(std::move(entry));
	}

private:
	async_event_builder& add_signed_field(std::string_view name, std::int64_t value) {
		detail::owned_field entry{};
		entry.name = std::string(name);
		entry.value.type = field_type::signed_integer;
		entry.value.signed_integer = value;
		fields_.push_back(std::move(entry));
		return *this;
	}

	async_event_builder& add_unsigned_field(std::string_view name, std::uint64_t value) {
		detail::owned_field entry{};
		entry.name = std::string(name);
		entry.value.type = field_type::unsigned_integer;
		entry.value.unsigned_integer = value;
		fields_.push_back(std::move(entry));
		return *this;
	}

	async_event_builder& add_floating_field(std::string_view name, double value) {
		detail::owned_field entry{};
		entry.name = std::string(name);
		entry.value.type = field_type::floating_point;
		entry.value.floating_point = value;
		fields_.push_back(std::move(entry));
		return *this;
	}

	std::shared_ptr<detail::async_state> state_{};
	std::string category_{};
	std::string event_name_{};
	std::vector<detail::owned_field> fields_{};
};

class async_logger_builder {
public:
	async_logger_builder& sink(std::shared_ptr<modern::log::sink> sink_target) {
		if (sink_target) {
			sinks_.push_back(std::move(sink_target));
		}
		return *this;
	}

	async_logger_builder& sink(modern::log::sink& sink_target) {
		sinks_.push_back(
			std::shared_ptr<modern::log::sink>(&sink_target, [](modern::log::sink*) {})
		);
		return *this;
	}

	async_logger_builder& queue_capacity(std::size_t value) {
		queue_contract_.capacity_records = value;
		return *this;
	}

	async_logger_builder& batch_limit(std::size_t value) {
		queue_contract_.batch_record_limit = value;
		return *this;
	}

	async_logger_builder& wakeup_threshold(std::size_t value) {
		queue_contract_.wakeup_record_threshold = value;
		return *this;
	}

	async_logger_builder& backpressure_policy(modern::log::backpressure_policy value) {
		policy_ = value;
		return *this;
	}

	template <typename Rep, typename Period>
	async_logger_builder& flush_interval(std::chrono::duration<Rep, Period> interval) {
		flush_interval_ = std::chrono::duration_cast<std::chrono::milliseconds>(interval);
		return *this;
	}

	template <typename Scheduler>
	async_logger_builder& scheduler(Scheduler&& scheduler) {
		if constexpr (std::constructible_from<modern::scheduler, Scheduler>) {
			runtime_scheduler_ = modern::scheduler(std::forward<Scheduler>(scheduler));
		} else if constexpr (requires(Scheduler&& candidate) {
			std::forward<Scheduler>(candidate).get_scheduler();
		}) {
			runtime_scheduler_ = std::forward<Scheduler>(scheduler).get_scheduler();
		} else {
			static_assert(sizeof(Scheduler) == 0, "scheduler() requires modern::scheduler or a type exposing get_scheduler()");
		}

		return *this;
	}

	[[nodiscard]] async_logger build() const {
		if (!detail::valid(queue_contract_)) {
			throw std::invalid_argument("invalid async logger queue contract");
		}

		return async_logger{
			std::make_shared<detail::async_state>(
				std::make_shared<std::vector<std::shared_ptr<modern::log::sink>>>(sinks_),
				queue_contract_,
				lifecycle_contract_,
				policy_,
				flush_interval_,
				runtime_scheduler_
			)
		};
	}

private:
	std::vector<std::shared_ptr<modern::log::sink>> sinks_{};
	detail::queue_contract queue_contract_{};
	detail::lifecycle_contract lifecycle_contract_{};
	modern::log::backpressure_policy policy_{modern::log::backpressure_policy::drop_newest};
	std::chrono::milliseconds flush_interval_{0};
	modern::scheduler runtime_scheduler_{};
};

inline async_logger_builder async_logger::builder() {
	return async_logger_builder{};
}

inline async_event_builder async_logger::event(std::string_view event_name) const {
	return async_event_builder{state_, category_, std::string(event_name)};
}

} // namespace modern::log