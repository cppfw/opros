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

#include <cerrno>
#include <cstdint>
#include <sstream>
#include <stdexcept>
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

struct event_info {
	waitable* object;
	utki::flags<ready> flags;
};

/**
 * @brief Set of waitable objects to wait for.
 */
class wait_set
{
	const unsigned wait_set_capacity;
	unsigned size_of_wait_set = 0;

#if CFG_OS == CFG_OS_WINDOWS
	std::vector<waitable*> waitables;
	std::vector<HANDLE> handles; // used to pass array of HANDLEs to WaitForMultipleObjectsEx()

#elif CFG_OS == CFG_OS_LINUX
	int epollSet;

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
		close(this->epollSet);
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
	 */
	void add(waitable& w, utki::flags<ready> wait_for);

	/**
	 * @brief Change wait flags for a given waitable.
	 * Changes wait flags for a given waitable, which is in this wait_set.
	 * @param w - waitable for which the changing of wait flags is needed.
	 * @param wait_for - new wait flags to be set for the given waitable.
	 */
	void change(waitable& w, utki::flags<ready> wait_for);

	/**
	 * @brief Remove waitable from wait set.
	 * @param w - waitable object to be removed from the wait_set.
	 */
	void remove(waitable& w) noexcept;

	/**
	 * @brief wait for event.
	 * This function blocks calling thread execution until one of the waitable
	 * objects in the wait_set triggers. Upon return from the function, pointers
	 * to triggered objects are placed in the 'out_events' buffer and the return
	 * value from the function indicates number of these objects which have
	 * triggered. Note, that it does not change the readiness state of
	 * non-triggered objects.
	 * @param out_events - pointer to buffer where to put pointers to triggered
	 * waitable objects. If the buffer size is not enough to hold all triggered
	 * waitables, then some of them will not be reported as triggered.
	 * @return number of objects triggered.
	 *         NOTE: for some reason, on Windows it can return 0 objects
	 * triggered.
	 */
	unsigned wait(utki::span<event_info> out_events)
	{
		return this->wait_internal(true, 0, out_events);
	}

	/**
	 * @brief wait for event with timeout.
	 * The same as wait() function, but takes wait timeout as parameter. Thus,
	 * this function will wait for any event or until timeout is hit. Note, that
	 * it guarantees that it will wait AT LEAST for specified number of
	 * milliseconds.
	 * @param timeout - maximum time in milliseconds to wait.
	 * @param out_events - buffer where to put pointers to triggered waitable
	 * objects. If the buffer size is not enough to hold all triggered
	 * waitables, then some of them will not be reported as triggered.
	 * @return number of objects triggered. If 0 then timeout was hit.
	 *         NOTE: for some reason, on Windows it can return 0 before timeout
	 * was hit.
	 */
	unsigned wait(uint32_t timeout, utki::span<event_info> out_events)
	{
		return this->wait_internal(false, timeout, out_events);
	}

private:
	unsigned wait_internal(bool infinite, uint32_t timeout, utki::span<event_info> out_events);

#if CFG_OS == CFG_OS_LINUX
	unsigned wait_internal_linux(int timeout, utki::span<event_info> out_events);
#endif

#if CFG_OS == CFG_OS_MACOSX
	void add_filter(waitable& w, int16_t filter);
	void remove_filter(waitable& w, int16_t filter) noexcept;
#endif
};

} // namespace opros
