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

#if CFG_OS == CFG_OS_MACOSX
#	include <sys/time.h>
#endif

using namespace opros;

wait_set::wait_set(unsigned max_size) :
	max_size_of_wait_set(max_size)
#if CFG_OS == CFG_OS_WINDOWS
	,
	waitables(max_size),
	handles(max_size)
{
	ASSERT(max_size <= MAXIMUM_WAIT_OBJECTS, [&](auto& o) {
		o << "max_size should be less than " << MAXIMUM_WAIT_OBJECTS;
	})
	if (max_size > MAXIMUM_WAIT_OBJECTS) {
		throw std::invalid_argument("wait_set::wait_set(): requested wait_set maximum size is too big");
	}
}

#elif CFG_OS == CFG_OS_LINUX
	,
	revents(max_size)
{
	if (max_size > std::numeric_limits<int>::max()) {
		throw std::invalid_argument("wait_set(): given max_size is too big, should be <= INT_MAX");
	}
	ASSERT(int(max_size) > 0)
	this->epollSet = epoll_create(int(max_size));
	if (this->epollSet < 0) {
		throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): epoll_create() failed");
	}
}
#elif CFG_OS == CFG_OS_MACOSX
{
	if (std::numeric_limits<decltype(max_size)>::max()
		>= std::numeric_limits<decltype(this->revents)::size_type>::max() / 2)
	{
		// the first 'if' is to prevent compiler warning that comparison of max_size
		// with too big constant number is always true
		if (max_size > std::numeric_limits<decltype(this->revents)::size_type>::max() / 2) {
			throw std::invalid_argument(
				"wait_set(): given max_size is too big, "
				"should be less than max(size_t) / 2"
			);
		}
	}
	this->revents.resize(max_size * 2);
	this->queue = kqueue();
	if (this->queue == -1) {
		throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): kqueue creation failed");
	}
}
#else
#	error "Unsupported OS"
#endif

#if CFG_OS == CFG_OS_MACOSX

void wait_set::add_filter(waitable& w, int16_t filter)
{
	struct kevent e;

	EV_SET(&e, w.handle, filter, EV_ADD | EV_RECEIPT, 0, 0, (void*)&w);

	const timespec timeout = {0, 0}; // 0 to make effect of polling, because passing
									 // NULL will cause to wait indefinitely.

	int res = kevent(this->queue, &e, 1, &e, 1, &timeout);
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

void wait_set::remove_filter(waitable& w, int16_t filter)
{
	struct kevent e;

	EV_SET(&e, w.handle, filter, EV_DELETE | EV_RECEIPT, 0, 0, 0);

	const timespec timeout = {0, 0}; // 0 to make effect of polling, because passing
									 // NULL will cause to wait indefinitely.

	int res = kevent(this->queue, &e, 1, &e, 1, &timeout);
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

void wait_set::add(waitable& w, utki::flags<ready> wait_for)
{
	//		TRACE(<< "wait_set::add(): enter" << std::endl)
	ASSERT(!w.is_added())

#if CFG_OS == CFG_OS_WINDOWS
	ASSERT(this->size() <= this->handles.size())
	if (this->size() == this->handles.size()) {
		throw std::logic_error("wait_set::add(): wait set is full");
	}

	// NOTE: Setting wait flags may throw an exception, so do that before
	// adding object to the array and incrementing number of added objects.
	w.waiting_object.set_waiting_flags(wait_for);

	this->handles[this->size_of_wait_set] = w.waiting_object.handle;
	this->waitables[this->size_of_wait_set] = &w;

#elif CFG_OS == CFG_OS_LINUX
	epoll_event e;
	e.data.fd = w.handle;
	e.data.ptr = &w;
	e.events = (wait_for.get(ready::read) ? (unsigned(EPOLLIN) | unsigned(EPOLLPRI)) : 0)
		| (wait_for.get(ready::write) ? EPOLLOUT : 0) | (EPOLLERR);
	int res = epoll_ctl(this->epollSet, EPOLL_CTL_ADD, w.handle, &e);
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
		this->add_filter(w, EVFILT_READ);
	}
	if (wait_for.get(ready::write)) {
		this->add_filter(w, EVFILT_WRITE);
	}
#else
#	error "Unsupported OS"
#endif

	++this->size_of_wait_set;

	w.is_added_to_waitset = true;
	//		TRACE(<< "wait_set::add(): exit" << std::endl)
}

void wait_set::change(waitable& w, utki::flags<ready> wait_for)
{
	ASSERT(w.is_added())

#if CFG_OS == CFG_OS_WINDOWS
	// check if the waitable object is added to this wait set
	{
		unsigned i;
		for (i = 0; i < this->size(); ++i) {
			if (this->waitables[i] == &w) {
				break;
			}
		}
		ASSERT(i <= this->size())
		if (i == this->size()) {
			throw std::logic_error("wait_set::change(): the waitable is not added to this wait set");
		}
	}

	// set new wait flags
	w.waiting_object.set_waiting_flags(wait_for);

#elif CFG_OS == CFG_OS_LINUX
	epoll_event e;
	e.data.fd = w.handle;
	e.data.ptr = &w;
	e.events = (wait_for.get(ready::read) ? (unsigned(EPOLLIN) | unsigned(EPOLLPRI)) : 0)
		| (wait_for.get(ready::write) ? EPOLLOUT : 0) | (EPOLLERR);
	int res = epoll_ctl(this->epollSet, EPOLL_CTL_MOD, w.handle, &e);
	if (res < 0) {
		throw std::system_error(errno, std::generic_category(), "wait_set::change(): epoll_ctl() failed");
	}
#elif CFG_OS == CFG_OS_MACOSX
	if (wait_for.get(ready::read)) {
		this->add_filter(w, EVFILT_READ);
	} else {
		this->remove_filter(w, EVFILT_READ);
	}
	if (wait_for.get(ready::write)) {
		this->add_filter(w, EVFILT_WRITE);
	} else {
		this->remove_filter(w, EVFILT_WRITE);
	}
#else
#	error "Unsupported OS"
#endif
}

void wait_set::remove(waitable& w) noexcept
{
	ASSERT(w.is_added())

	ASSERT(this->size() != 0)

#if CFG_OS == CFG_OS_WINDOWS
	// remove object from array
	{
		unsigned i;
		for (i = 0; i < this->size_of_wait_set; ++i) {
			if (this->waitables[i] == &w) {
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
	w.waiting_object.set_waiting_flags(false);

#elif CFG_OS == CFG_OS_LINUX
	int res = epoll_ctl(this->epollSet, EPOLL_CTL_DEL, w.handle, nullptr);
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

	w.is_added_to_waitset = false;
	//		TRACE(<< "wait_set::remove(): completed successfuly" <<
	// std::endl)
}

#if CFG_OS == CFG_OS_LINUX

unsigned wait_set::wait_internal_linux(int timeout, utki::span<event_info> out_events)
{
	// TRACE(<< "going to epoll_wait() with timeout = " << timeout << std::endl)

	int res;

	while (true) {
		ASSERT(this->revents.size() <= std::numeric_limits<int>::max())
		res = epoll_wait(this->epollSet, this->revents.data(), int(this->revents.size()), timeout);

		// TRACE(<< "epoll_wait() returned " << res << std::endl)

		if (res < 0) {
			// if interrupted by signal, try waiting again.
			if (errno == EINTR) {
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "wait_set::wait(): epoll_wait() failed");
		}
		break;
	};

	ASSERT(res >= 0)
	ASSERT(unsigned(res) <= this->revents.size())

	unsigned num_events_stored = 0;
	for (epoll_event* e = this->revents.data(); e < this->revents.data() + res; ++e) {
		auto w = static_cast<waitable*>(e->data.ptr);
		ASSERT(w)

		if (num_events_stored < out_events.size()) {
			event_info& ei = out_events[num_events_stored];
			++num_events_stored;

			ei.w = w;
			ei.flags.clear();

			if ((e->events & EPOLLERR) != 0) {
				ei.flags.set(ready::error);
			}
			if ((e->events & (unsigned(EPOLLIN) | unsigned(EPOLLPRI))) != 0) {
				ei.flags.set(ready::read);
			}
			if ((e->events & EPOLLOUT) != 0) {
				ei.flags.set(ready::write);
			}

			ASSERT(!ei.flags.is_clear())
		}
	}

	ASSERT(res >= 0) // NOTE: 'res' can be zero, if no events happened in the
					 // specified timeout
	return unsigned(res);
}

#endif

unsigned wait_set::wait_internal(bool wait_infinitly, uint32_t timeout, utki::span<event_info> out_events)
{
	if (this->size_of_wait_set == 0) {
		throw std::logic_error(
			"wait_set::wait(): no waitable objects were added "
			"to the wait_set, can't perform wait()"
		);
	}

#if CFG_OS == CFG_OS_WINDOWS
	DWORD wait_timeout;
	if (wait_infinitly) {
		wait_timeout = INFINITE;
	} else {
		static_assert(INFINITE == 0xffffffff, "check that INFINITE macro is max uint32_T failed");
		if (timeout == 0xffffffff) {
			wait_timeout = 0xffffffff - 1;
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
			GetLastError(),
			std::generic_category(),
			"wait_set::wait(): WaitForMultipleObjectsEx() failed"
		);
	}

	if (res == WAIT_TIMEOUT) {
		return 0;
	}

	ASSERT(WAIT_OBJECT_0 <= res && res < (WAIT_OBJECT_0 + this->size_of_wait_set))

	// check for activities
	unsigned num_events = 0;
	for (unsigned i = 0; i < this->size_of_wait_set; ++i) {
		// NOTE: Need to call get_readiness_flags() even if 'num_events < out_events.size()',
		// because it resets the readiness state of the HANDLE.
		auto flags = this->waitables[i]->waiting_object.get_readiness_flags();

		if (!flags.is_clear()) {
			if (num_events < out_events.size()) {
				out_events[num_events].w = this->waitables[i];
				out_events[num_events].flags = flags;
				++num_events;
			}
		} else {
			// NOTE: sometimes the event is reported as signaled, but no read/write
			// events indicated.
			//       Don't know why it happens.
			//			ASSERT_INFO(i != (res - WAIT_OBJECT_0), "i = "
			//<< i << " (res - WAIT_OBJECT_0) = " << (res - WAIT_OBJECT_0) << "
			// wait_flags = " << this->waitables[i]->readiness_flags)
		}
	}

	// NOTE: Sometimes the event is reported as signaled, but no actual activity
	// is there.
	//       Don't know why.
	//		ASSERT(num_events > 0)

	return num_events;

#elif CFG_OS == CFG_OS_LINUX
	if (wait_infinitly) {
		return this->wait_internal_linux(-1, out_events);
	}

	// in linux, epoll_wait() gets timeout as int argument, while we have timeout
	// as uint32_t, so the requested timeout can be bigger than int can hold
	// (negative values of the int are not used)

	auto max_time_step = uint32_t(std::numeric_limits<int>::max());

	while (timeout >= max_time_step) {
		ASSERT(int(max_time_step) >= 0, [&](auto& o) {
			o << "timeout = 0x" << std::hex << timeout;
		})
		auto res = this->wait_internal_linux(int(max_time_step), out_events);
		if (res != 0) {
			return res;
		}
		timeout -= max_time_step;
		if (timeout == 0) {
			return 0;
		}
	}

	ASSERT(int(timeout) >= 0)
	return this->wait_internal_linux(int(timeout), out_events);

#elif CFG_OS == CFG_OS_MACOSX
	struct timespec ts = {
		decltype(timespec::tv_sec)(timeout / 1000), // seconds
		decltype(timespec::tv_nsec)((timeout % 1000) * 1000000) // nanoseconds
	};

	for (;;) {
		int res =
			kevent(this->queue, 0, 0, this->revents.data(), int(this->revents.size()), (wait_infinitly) ? 0 : &ts);

		if (res < 0) {
			if (errno == EINTR) {
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "wait_set::wait(): kevent() failed");
		} else if (res == 0) {
			return 0; // timeout
		} else if (res > 0) {
			unsigned out_i = 0; // index into out_events
			for (unsigned i = 0; i != unsigned(res); ++i) {
				if (out_i < out_events.size()) {
					const auto& e = this->revents[i];
					auto w = reinterpret_cast<waitable*>(e.udata);

					auto& oe = out_events[out_i];
					oe.flags.clear();

					if (e.filter == EVFILT_WRITE) {
						oe.flags.set(ready::write);
					} else if (e.filter == EVFILT_READ) {
						oe.flags.set(ready::read);
					}

					if ((e.flags & EV_ERROR) != 0) {
						oe.flags.set(ready::error);
					}

					// check if waitable is already added
					unsigned k = 0;
					for (; k != out_i; ++k) {
						if (out_events[k].w == w) {
							break;
						}
					}
					if (k == out_i) {
						oe.w = w;
						++out_i;
					}
				}
			}
			return unsigned(res);
		}
	}
#else
#	error "Unsupported OS"
#endif
}
