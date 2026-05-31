#pragma once

#include <array>
#include <cstddef>
#include <memory_resource>

namespace modern::log::detail {

template <std::size_t InlineBytes = 2048>
class field_arena final {
public:
	field_arena()
		: resource_(buffer_.data(), buffer_.size()) {}

	field_arena(const field_arena&) = delete;
	field_arena& operator=(const field_arena&) = delete;
	field_arena(field_arena&&) = delete;
	field_arena& operator=(field_arena&&) = delete;

	[[nodiscard]] std::pmr::memory_resource* resource() noexcept {
		return &resource_;
	}

private:
	alignas(std::max_align_t) std::array<std::byte, InlineBytes> buffer_{};
	std::pmr::monotonic_buffer_resource resource_;
};

} // namespace modern::log::detail