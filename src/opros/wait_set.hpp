/*
The MIT License (MIT)

Copyright (c) 2015-2023 Ivan Gagis <igagis@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* ================ LICENSE END ================ */

#pragma once

#include <array>
#include <cerrno>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

#include <utki/config.hpp>
#include <utki/debug.hpp>
#include <utki/span.hpp>

#if CFG_OS == CFG_OS_WINDOWS
#	include <utki/windows.hpp>

#elif CFG_OS == CFG_OS_LINUX
#	include <sys/epoll.h>
#	include <unistd.h>

#elif CFG_OS == CFG_OS_MACOSX
#	include <sys/event.h>
#	include <sys/types.h>
#	include <unistd.h>

#else
#	error "Unsupported OS"
#endif

#include "waitable.hpp"

#ifdef assert
#	undef assert
#endif

namespace opros {

// TODO: doxygen
struct event_info {
	utki::flags<ready> flags;
	void* user_data;
};

/**
 * @brief Set of waitable objects to wait for.
 */
class wait_set
{
	const unsigned wait_set_capacity;
	unsigned size_of_wait_set = 0;

	// for small wait_set we use static array instead of vector
	constexpr static const unsigned static_capacity_threshold = 3;

	using out_events_array_type = std::array<event_info, static_capacity_threshold>;
	using out_events_vector_type = std::vector<event_info>;

	// define the buffer which will hold triggered events info
	std::variant<out_events_array_type, out_events_vector_type> out_events_variant;

	utki::span<event_info> get_out_events() noexcept
	{
		try {
			if (std::holds_alternative<out_events_array_type>(this->out_events_variant)) {
				auto& a = std::get<out_events_array_type>(this->out_events_variant);
				ASSERT(this->wait_set_capacity <= a.size())
				return utki::make_span(a.data(), this->wait_set_capacity);
			}
			ASSERT(std::holds_alternative<out_events_vector_type>(this->out_events_variant))
			return std::get<out_events_vector_type>(this->out_events_variant);
		} catch (std::bad_variant_access&) {
			ASSERT(false)
			return nullptr;
		}
	}

	utki::span<const event_info> triggered{nullptr};

#if CFG_OS == CFG_OS_WINDOWS
	struct added_waitable_info {
		waitable* w;
		void* user_data;
	};

	std::vector<added_waitable_info> waitables;
	std::vector<HANDLE> handles; // used to pass array of HANDLEs to WaitForMultipleObjectsEx()

#elif CFG_OS == CFG_OS_LINUX
	int epoll_set;

	std::vector<epoll_event> revents; // used for getting the result from epoll_wait()
#elif CFG_OS == CFG_OS_MACOSX
	int queue; // kqueue

	std::vector<struct kevent> revents; // used for getting the result
#else
#	error "Unsupported OS"
#endif

public:
	/**
	 * @brief Constructor.
	 * @param capacity - maximum number of waitable objects that can be added to
	 * the wait set.
	 */
	wait_set(unsigned capacity);

	/**
	 * @brief Destructor.
	 * Note, that destructor will check if the wait set is empty. If it is not,
	 * then an assert will be triggered. It is user's responsibility to remove any
	 * waitable objects from the waitset before the wait set object is destroyed.
	 */
	~wait_set() noexcept
	{
		utki::assert(
			this->size_of_wait_set == 0,
			[](auto& o) {
				o << "attempt to destroy wait_set containig waitables";
			},
			SL
		);
#if CFG_OS == CFG_OS_WINDOWS
		// do nothing
#elif CFG_OS == CFG_OS_LINUX
		close(this->epoll_set);
#elif CFG_OS == CFG_OS_MACOSX
		close(this->queue);
#else
#	error "Unsupported OS"
#endif
	}

	/**
	 * @brief Get maximum size of the wait set.
	 * @return maximum number of waitables this wait_set can hold.
	 */
	unsigned capacity() const noexcept
	{
		return this->wait_set_capacity;
	}

	/**
	 * @brief Get number of waitables already added to the wait_set.
	 * @return number of waitables added to the wait_set.
	 */
	unsigned size() const noexcept
	{
		return this->size_of_wait_set;
	}

	/**
	 * @brief Add waitable object to the wait set.
	 * @param w - waitable object to add to the wait_set.
	 * @param wait_for - determine events waiting for which we are interested.
	 * @param user_data - user data associated with the waitable object.
	 */
	void add(waitable& w, utki::flags<ready> wait_for, void* user_data);

	/**
	 * @brief Change wait flags for a given waitable.
	 * Changes wait flags for a given waitable, which is in this wait_set.
	 * @param w - waitable for which the changing of wait flags is needed.
	 * @param wait_for - new wait flags to be set for the given waitable.
	 * @param user_data - user data associated with the waitable object.
	 */
	void change(waitable& w, utki::flags<ready> wait_for, void* user_data);

	/**
	 * @brief Remove waitable from wait set.
	 * @param w - waitable object to be removed from the wait_set.
	 */
	void remove(waitable& w) noexcept;

	/**
	 * @brief wait for event.
	 * This function blocks calling thread execution until one of the waitable
	 * objects in the wait_set triggers.
	 */
	void wait()
	{
		[[maybe_unused]] bool res = this->wait_internal(true, 0);
		ASSERT(res)
	}

	/**
	 * @brief wait for event with timeout.
	 * The same as wait() function, but takes wait timeout as parameter. Thus,
	 * this function will wait for any event or until timeout is hit. Note, that
	 * it guarantees that it will wait AT LEAST for specified number of
	 * milliseconds.
	 * @param timeout - maximum time in milliseconds to wait.
	 * @return true in case the function returned before the timeout has elapsed.
	 * @return false in case the function has returned due to the timeout.
	 */
	bool wait(uint32_t timeout)
	{
		return this->wait_internal(false, timeout);
	}

	/**
	 * @brief Get triggered events since last call to wait().
	 * @return Triggered events since last call of wait() function.
	 */
	utki::span<const event_info> get_triggered() const noexcept
	{
		return this->triggered;
	}

private:
	bool wait_internal(bool infinite, uint32_t timeout);

#if CFG_OS == CFG_OS_LINUX
	bool wait_internal_linux(int timeout);
#endif

#if CFG_OS == CFG_OS_MACOSX
	void add_filter(waitable& w, int16_t filter, void* user_data);
	void remove_filter(waitable& w, int16_t filter) noexcept;
#endif
};

} // namespace opros
