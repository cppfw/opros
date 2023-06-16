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

#include "wait_set.hpp"

#include <cstring>

#include <utki/time.hpp>

#if CFG_OS == CFG_OS_MACOSX
#	include <sys/time.h>
#endif

using namespace opros;

wait_set::wait_set(unsigned capacity) :
	wait_set_capacity(capacity),
	out_events_variant([this]() {
		decltype(this->out_events_variant) ret;
		if (this->wait_set_capacity <= static_capacity_threshold) {
			ret.emplace<out_events_array_type>();
		} else {
			ret.emplace<out_events_vector_type>(this->wait_set_capacity);
		}
		return ret;
	}())
#if CFG_OS == CFG_OS_WINDOWS
	,
	waitables(capacity),
	handles(capacity)
{
	ASSERT(capacity <= MAXIMUM_WAIT_OBJECTS, [&](auto& o) {
		o << "capacity should be less than " << MAXIMUM_WAIT_OBJECTS;
	})
	if (capacity > MAXIMUM_WAIT_OBJECTS) {
		throw std::invalid_argument("wait_set::wait_set(): requested wait_set maximum size is too big");
	}
}

#elif CFG_OS == CFG_OS_LINUX
	,
	revents(capacity)
{
	if (capacity > std::numeric_limits<int>::max()) {
		throw std::invalid_argument("wait_set(): given capacity is too big, should be <= INT_MAX");
	}
	ASSERT(int(capacity) > 0)
	this->epoll_set = epoll_create(int(capacity));
	if (this->epoll_set < 0) {
		throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): epoll_create() failed");
	}
}
#elif CFG_OS == CFG_OS_MACOSX
	,
	// kevent() reports read and write events separately, so the total number of simultaneous events
	// reported by kevent() can be more than the total number of waitable objects waited on,
	// but it is ok to use buffer with less capacity to get the triggered events, then the events
	// which did not fit into the buffer will be reported the next time.
	// Using the buffer of the same size as wait_set capacity makes it easier to unify the behaviour
	// across different platforms.
	revents(capacity)
{
	if (capacity > std::numeric_limits<int>::max()) {
		throw std::invalid_argument("wait_set(): given capacity is too big, should be <= INT_MAX");
	}
	ASSERT(int(capacity) > 0)
	this->queue = kqueue();
	if (this->queue == -1) {
		throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): kqueue creation failed");
	}
}
#else
#	error "Unsupported OS"
#endif

#if CFG_OS == CFG_OS_MACOSX

void wait_set::add_filter(waitable& w, int16_t filter, void* user_data)
{
	struct kevent e {};

	EV_SET(&e, w.handle, filter, EV_ADD | EV_RECEIPT, 0, 0, user_data);

	// 0 to make effect of polling, because passing
	// NULL will cause to wait indefinitely.
	const timespec timeout = {0, 0};

	int res = kevent(this->queue, &e, 1, nullptr, 0, &timeout);
	if (res < 0) {
		throw std::system_error(errno, std::generic_category(), "wait_set::add(): add_filter(): kevent() failed");
	}

	ASSERT((e.flags & EV_ERROR) != 0) // EV_ERROR is always returned because of EV_RECEIPT, according to
									  // kevent() documentation.
	if (e.data != 0) { // data should be 0 if added successfully
		LOG([&](auto& o) {
			o << "wait_set::add(): e.data = " << e.data << std::endl;
		})
		throw std::runtime_error("wait_set::add(): add_filter(): kevent() failed to add filter");
	}
}

void wait_set::remove_filter(waitable& w, int16_t filter) noexcept
{
	struct kevent e {};

	EV_SET(&e, w.handle, filter, EV_DELETE | EV_RECEIPT, 0, 0, nullptr);

	const timespec timeout = {0, 0}; // 0 to make effect of polling, because passing
									 // NULL will cause to wait indefinitely.

	int res = kevent(this->queue, &e, 1, nullptr, 0, &timeout);
	if (res < 0) {
		// ignore the failure
		LOG([&](auto& o) {
			o << "wait_set::remove(): remove_filter(): kevent() failed" << std::endl;
		})
	}

	ASSERT((e.flags & EV_ERROR) != 0) // EV_ERROR is always returned because of EV_RECEIPT, according to
									  // kevent() documentation.
}

#endif

void wait_set::add(waitable& w, utki::flags<ready> wait_for, void* user_data)
{
#if CFG_OS == CFG_OS_WINDOWS
	ASSERT(this->size() <= this->handles.size())
	if (this->size() == this->handles.size()) {
		throw std::logic_error("wait_set::add(): wait set is full");
	}

	// NOTE: Setting wait flags may throw an exception, so do that before
	// adding object to the array and incrementing number of added objects.
	w.set_waiting_flags(wait_for);

	this->handles[this->size_of_wait_set] = w.handle;
	{
		auto& wi = this->waitables[this->size_of_wait_set];
		wi.w = &w;
		wi.user_data = user_data;
	}

#elif CFG_OS == CFG_OS_LINUX
	epoll_event e{};
	e.data.fd = w.handle;
	e.data.ptr = user_data;
	e.events = (wait_for.get(ready::read) ? (unsigned(EPOLLIN) | unsigned(EPOLLPRI)) : 0)
		| (wait_for.get(ready::write) ? EPOLLOUT : 0) | (EPOLLERR);
	int res = epoll_ctl(this->epoll_set, EPOLL_CTL_ADD, w.handle, &e);
	if (res < 0) {
		LOG([&](auto& o) {
			o << "wait_set::add(): epoll_ctl() failed. If you are adding socket, "
				 "please check that is is opened before adding to wait_set."
			  << std::endl;
		})
		throw std::system_error(errno, std::generic_category(), "wait_set::add(): epoll_ctl() failed");
	}
#elif CFG_OS == CFG_OS_MACOSX
	ASSERT(this->size() <= this->revents.size() / 2)

	if (wait_for.get(ready::read)) {
		this->add_filter(w, EVFILT_READ, user_data);
	}
	if (wait_for.get(ready::write)) {
		this->add_filter(w, EVFILT_WRITE, user_data);
	}
#else
#	error "Unsupported OS"
#endif

	++this->size_of_wait_set;
}

void wait_set::change(waitable& w, utki::flags<ready> wait_for, void* user_data)
{
#if CFG_OS == CFG_OS_WINDOWS
	// check if the waitable object is added to this wait set
	{
		unsigned i = 0;
		for (; i < this->size(); ++i) {
			if (this->waitables[i].w == &w) {
				break;
			}
		}
		ASSERT(i <= this->size())
		if (i == this->size()) {
			throw std::logic_error("wait_set::change(): the waitable is not added to this wait set");
		}
	}

	// set new wait flags
	w.set_waiting_flags(wait_for);

#elif CFG_OS == CFG_OS_LINUX
	epoll_event e{};
	e.data.fd = w.handle;
	e.data.ptr = user_data;
	e.events = (wait_for.get(ready::read) ? (unsigned(EPOLLIN) | unsigned(EPOLLPRI)) : 0)
		| (wait_for.get(ready::write) ? EPOLLOUT : 0) | (EPOLLERR);
	int res = epoll_ctl(this->epoll_set, EPOLL_CTL_MOD, w.handle, &e);
	if (res < 0) {
		throw std::system_error(errno, std::generic_category(), "wait_set::change(): epoll_ctl() failed");
	}
#elif CFG_OS == CFG_OS_MACOSX
	if (wait_for.get(ready::read)) {
		this->add_filter(w, EVFILT_READ, user_data);
	} else {
		this->remove_filter(w, EVFILT_READ);
	}
	if (wait_for.get(ready::write)) {
		this->add_filter(w, EVFILT_WRITE, user_data);
	} else {
		this->remove_filter(w, EVFILT_WRITE);
	}
#else
#	error "Unsupported OS"
#endif
}

void wait_set::remove(waitable& w) noexcept
{
	ASSERT(this->size() != 0)

#if CFG_OS == CFG_OS_WINDOWS
	// remove object from array
	{
		unsigned i = 0;
		for (; i < this->size_of_wait_set; ++i) {
			if (this->waitables[i].w == &w) {
				break;
			}
		}
		ASSERT(i <= this->size_of_wait_set)
		ASSERT(i != this->size_of_wait_set, [&](auto& o) {
			o << "wait_set::remove(): waitable is not added to wait set";
		})

		unsigned num_object = this->size_of_wait_set - 1; // decrease number of objects before
														  // shifting the object handles in the array
		// shift object handles in the array
		for (; i < num_object; ++i) {
			this->handles[i] = this->handles[i + 1];
			this->waitables[i] = this->waitables[i + 1];
		}
	}

	// clear wait flags
	try {
		w.set_waiting_flags(false);
	} catch (...) {
		// ignore error
	}

#elif CFG_OS == CFG_OS_LINUX
	int res = epoll_ctl(this->epoll_set, EPOLL_CTL_DEL, w.handle, nullptr);
	if (res < 0) {
		ASSERT(false, [&](auto& o) {
			o << "wait_set::Remove(): epoll_ctl failed, probably the waitable was "
				 "not added to the wait set";
		})
	}
#elif CFG_OS == CFG_OS_MACOSX
	this->remove_filter(w, EVFILT_READ);
	this->remove_filter(w, EVFILT_WRITE);
#else
#	error "Unsupported OS"
#endif

	--this->size_of_wait_set;
}

#if CFG_OS == CFG_OS_LINUX

bool wait_set::wait_internal_linux(int timeout)
{
	// TRACE(<< "going to epoll_wait() with timeout = " << timeout << std::endl)

	auto out_events = this->get_out_events();

	int num_events_triggered{};

	while (true) {
		ASSERT(this->revents.size() <= std::numeric_limits<int>::max())
		num_events_triggered = epoll_wait(this->epoll_set, this->revents.data(), int(this->revents.size()), timeout);

		// TRACE(<< "epoll_wait() returned " << num_events_triggered << std::endl)

		if (num_events_triggered < 0) {
			// if interrupted by signal, try waiting again.
			if (errno == EINTR) {
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "wait_set::wait(): epoll_wait() failed");
		}
		break;
	};

	if (num_events_triggered == 0) {
		// timeout hit
		this->triggered = nullptr;
		return false;
	}

	ASSERT(num_events_triggered > 0)
	ASSERT(this->revents.size() == out_events.size())
	ASSERT(size_t(num_events_triggered) <= this->revents.size())
	ASSERT(size_t(num_events_triggered) <= out_events.size())

	unsigned out_i = 0;
	for (const auto& e : utki::make_span(this->revents.data(), num_events_triggered)) {
		ASSERT(out_i < out_events.size())
		event_info& ei = out_events[out_i];
		++out_i;

		ei.flags.clear();
		ei.user_data = e.data.ptr;

		if ((e.events & EPOLLERR) != 0) {
			ei.flags.set(ready::error);
		}
		if ((e.events & (unsigned(EPOLLIN) | unsigned(EPOLLPRI))) != 0) {
			ei.flags.set(ready::read);
		}
		if ((e.events & EPOLLOUT) != 0) {
			ei.flags.set(ready::write);
		}

		ASSERT(!ei.flags.is_clear())
	}

	ASSERT(out_i > 0)
	ASSERT(size_t(out_i) <= out_events.size())
	ASSERT(out_i == unsigned(num_events_triggered))
	this->triggered = utki::make_span(out_events.data(), num_events_triggered);

	return true;
}

#endif

bool wait_set::wait_internal(bool wait_infinitly, uint32_t timeout)
{
	if (this->size_of_wait_set == 0) {
		throw std::logic_error(
			"wait_set::wait(): no waitable objects were added "
			"to the wait_set, can't perform wait()"
		);
	}

#if CFG_OS == CFG_OS_WINDOWS
	DWORD wait_timeout{};
	if (wait_infinitly) {
		wait_timeout = INFINITE;
	} else {
		static_assert(
			INFINITE == std::numeric_limits<DWORD>::max(),
			"check that INFINITE macro is max uint32_t failed"
		);
		if (timeout == std::numeric_limits<decltype(timeout)>::max()) {
			wait_timeout = std::numeric_limits<DWORD>::max() - 1;
		} else {
			wait_timeout = DWORD(timeout);
		}
	}

	DWORD res = WaitForMultipleObjectsEx(
		this->size_of_wait_set,
		this->handles.data(),
		FALSE, // do not wait for all objects, wait for at least one
		wait_timeout,
		FALSE // do not stop waiting on IO completion
	);

	// Return value cannot be WAIT_IO_COMPLETION because we supplied FALSE as
	// last parameter to WaitForMultipleObjectsEx().
	ASSERT(res != WAIT_IO_COMPLETION)

	// we are not expecting abandoned mutexes
	ASSERT(res < WAIT_ABANDONED_0 || (WAIT_ABANDONED_0 + this->size_of_wait_set) <= res)

	if (res == WAIT_FAILED) {
		throw std::system_error(
			int(GetLastError()),
			std::generic_category(),
			"wait_set::wait(): WaitForMultipleObjectsEx() failed"
		);
	}

	if (res == WAIT_TIMEOUT) {
		this->triggered = nullptr;
		return false;
	}

	ASSERT(WAIT_OBJECT_0 <= res && res < (WAIT_OBJECT_0 + this->size_of_wait_set))

	auto out_events = this->get_out_events();
	ASSERT(out_events.size() == this->waitables.size())
	ASSERT(this->handles.size() == this->waitables.size())

	// check for activities
	unsigned num_events = 0;
	for (unsigned i = 0; i < this->size_of_wait_set; ++i) {
		auto& wi = this->waitables[i];

		// Check if handle is in signalled state.
		// In case we have auto-reset events (see
		// https://learn.microsoft.com/en-us/windows/win32/sync/event-objects?redirectedfrom=MSDN) the signalled state
		// of the event which made WaitForMultipleObjectsEx() to return will be reset, so we need to check if it is that
		// event by comparing index to what was returned by WaitForMultipleObjectsEx(). Otherwise, we call
		// WaitForSingleObjectEx() with zero timeout to check if the event was/is in signalled state.
		if (res - WAIT_OBJECT_0 == i
			|| WaitForSingleObjectEx(
				   wi.w->handle,
				   0, // 0 ms timeout
				   FALSE // do not stop waiting on IO completion
			   ) == WAIT_OBJECT_0)
		{
			// the object is in signalled state

			// NOTE: Need to call get_readiness_flags() even if 'num_events < out_events.size()',
			// because it resets the readiness state of the HANDLE.
			ASSERT(wi.w)
			auto flags = wi.w->get_readiness_flags();

			// WORKAROUND:
			// On Windows, sometimes event triggers, but then no readiness flags are reported.
			// As a workaround, here we need to check if there are any readiness
			// flags actually set.
			if (!flags.is_clear()) {
				ASSERT(num_events < out_events.size())

				out_events[num_events].user_data = wi.user_data;
				out_events[num_events].flags = flags;

				++num_events;
			}
		}
	}

	ASSERT(num_events <= this->size_of_wait_set)
	ASSERT(num_events <= out_events.size())
	this->triggered = utki::make_span(out_events.data(), num_events);

	return true;

#elif CFG_OS == CFG_OS_LINUX
	if (wait_infinitly) {
		return this->wait_internal_linux(-1);
	}

	// in linux, epoll_wait() gets timeout as int argument, while we have timeout
	// as uint32_t, so the requested timeout can be bigger than int can hold
	// (negative values of the int are not used)

	auto max_time_step = uint32_t(std::numeric_limits<int>::max());

	while (timeout >= max_time_step) {
		ASSERT(int(max_time_step) >= 0, [&](auto& o) {
			o << "timeout = 0x" << std::hex << timeout;
		})
		auto res = this->wait_internal_linux(int(max_time_step));
		if (res != 0) {
			return res;
		}
		timeout -= max_time_step;
		if (timeout == 0) {
			// timeout hit
			return false;
		}
	}

	ASSERT(int(timeout) >= 0)
	return this->wait_internal_linux(int(timeout));

#elif CFG_OS == CFG_OS_MACOSX
	struct timespec ts = {
		decltype(timespec::tv_sec)(timeout / utki::num_millisec_in_sec), // seconds
		decltype(timespec::tv_nsec)(
			(timeout % utki::num_millisec_in_sec) * utki::num_millisec_in_sec * utki::num_millisec_in_sec
		) // nanoseconds
	};

	for (;;) {
		ASSERT(this->revents.size() <= std::numeric_limits<int>::max())
		int num_events_triggered = kevent(
			this->queue,
			nullptr,
			0,
			this->revents.data(),
			int(this->revents.size()),
			(wait_infinitly) ? nullptr : &ts
		);

		if (num_events_triggered < 0) {
			if (errno == EINTR) {
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "wait_set::wait(): kevent() failed");
		}

		if (num_events_triggered == 0) {
			// timeout hit
			this->triggered = nullptr;
			return false;
		}

		ASSERT(num_events_triggered > 0)

		auto out_events = this->get_out_events();

		ASSERT(out_events.size() == this->revents.size())

		size_t out_i = 0; // index into out_events

		for (const auto& e : utki::make_span(this->revents.data(), size_t(num_events_triggered))) {
			utki::flags<opros::ready> flags{false};

			if ((e.flags & EV_ERROR) != 0) {
				flags.set(ready::error);
			} else {
				// no error condition, then set the flag based on filter type
				if (e.filter == EVFILT_WRITE) {
					flags.set(ready::write);
				} else if (e.filter == EVFILT_READ) {
					flags.set(ready::read);
				} else {
					// unsupported event, skip it
					continue;
				}
			}

			auto& oe = out_events[out_i];
			++out_i;

			oe.flags = flags;
			oe.user_data = e.udata;
		}

		ASSERT(out_i <= out_events.size())

		// out_i can be less than number of events triggered because there can be unsupported events
		// which are not counted
		ASSERT(out_i <= size_t(num_events_triggered))

		this->triggered = utki::make_span(out_events.data(), out_i);

		return true;
	}
#else
#	error "Unsupported OS"
#endif
}
