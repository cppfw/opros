#include "wait_set.hpp"

#include <cstring>

#if M_OS == M_OS_MACOSX
#	include <sys/time.h>
#endif

using namespace opros;

wait_set::wait_set(unsigned max_size) :
		max_size_of_wait_set(max_size)
#if M_OS == M_OS_WINDOWS
		,waitables(max_size)
		,handles(max_size)
{
	ASSERT_INFO(max_size <= MAXIMUM_WAIT_OBJECTS, "max_size should be less than " << MAXIMUM_WAIT_OBJECTS)
	if(max_size > MAXIMUM_WAIT_OBJECTS){
		throw std::invalid_argument("wait_set::wait_set(): requested wait_set maximum size is too big");
	}
}

#elif M_OS == M_OS_LINUX
		,revents(max_size)
{
	if(max_size > std::numeric_limits<int>::max()){
		throw std::invalid_argument("wait_set(): given max_size is too big, should be <= INT_MAX");
	}
	ASSERT(int(max_size) > 0)
	this->epollSet = epoll_create(int(max_size));
	if(this->epollSet < 0){
		throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): epoll_create() failed");
	}
}
#elif M_OS == M_OS_MACOSX
{
	if(std::numeric_limits<decltype(max_size)>::max() >= std::numeric_limits<decltype(this->revents)::size_type>::max() / 2){
		// the first 'if' is to prevent compiler warning that comparison of max_size with too big constant number is always true
		if(max_size > std::numeric_limits<decltype(this->revents)::size_type>::max() / 2){
			throw std::invalid_argument("wait_set(): given max_size is too big, should be less than max(size_t) / 2");
		}
	}
	this->revents.resize(max_size * 2);
	this->queue = kqueue();
	if(this->queue == -1){
		throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): kqueue creation failed");
	}
}
#else
#	error "Unsupported OS"
#endif

#if M_OS == M_OS_MACOSX

void wait_set::add_filter(waitable& w, int16_t filter){
	struct kevent e;

	EV_SET(&e, w.get_handle(), filter, EV_ADD | EV_RECEIPT, 0, 0, (void*)&w);

	const timespec timeout = {0, 0}; // 0 to make effect of polling, because passing NULL will cause to wait indefinitely.

	int res = kevent(this->queue, &e, 1, &e, 1, &timeout);
	if(res < 0){
		throw std::system_error(errno, std::generic_category(), "wait_set::add(): add_filter(): kevent() failed");
	}
	
	ASSERT((e.flags & EV_ERROR) != 0) // EV_ERROR is always returned because of EV_RECEIPT, according to kevent() documentation.
	if(e.data != 0){ // data should be 0 if added successfully
		TRACE(<< "wait_set::add(): e.data = " << e.data << std::endl)
		throw std::runtime_error("wait_set::add(): add_filter(): kevent() failed to add filter");
	}
}

void wait_set::remove_filter(waitable& w, int16_t filter){
	struct kevent e;

	EV_SET(&e, w.get_handle(), filter, EV_DELETE | EV_RECEIPT, 0, 0, 0);

	const timespec timeout = {0, 0}; // 0 to make effect of polling, because passing NULL will cause to wait indefinitely.

	int res = kevent(this->queue, &e, 1, &e, 1, &timeout);
	if(res < 0){
		// ignore the failure
		TRACE(<< "wait_set::remove(): remove_filter(): kevent() failed" << std::endl);
	}
	
	ASSERT((e.flags & EV_ERROR) != 0) // EV_ERROR is always returned because of EV_RECEIPT, according to kevent() documentation.
}

#endif

void wait_set::add(waitable& w, utki::flags<ready> wait_for){
//		TRACE(<< "wait_set::add(): enter" << std::endl)
	ASSERT(!w.is_added())

#if M_OS == M_OS_WINDOWS
	ASSERT(this->size() <= this->handles.size())
	if(this->size() == this->handles.size()){
		throw utki::invalid_state("wait_set::add(): wait set is full");
	}

	// NOTE: Setting wait flags may throw an exception, so do that before
	// adding object to the array and incrementing number of added objects.
	w.set_waiting_flags(wait_for);

	this->handles[this->size_of_wait_set] = w.get_handle();
	this->waitables[this->size_of_wait_set] = &w;

#elif M_OS == M_OS_LINUX
	epoll_event e;
	e.data.fd = w.get_handle();
	e.data.ptr = &w;
	e.events =
			(wait_for.get(ready::read) ? (EPOLLIN | EPOLLPRI) : 0)
			| (wait_for.get(ready::write) ? EPOLLOUT : 0)
			| (EPOLLERR);
	int res = epoll_ctl(
			this->epollSet,
			EPOLL_CTL_ADD,
			w.get_handle(),
			&e
		);
	if(res < 0){
		TRACE(<< "wait_set::add(): epoll_ctl() failed. If you are adding socket, please check that is is opened before adding to wait_set." << std::endl)
		throw std::system_error(errno, std::generic_category(), "wait_set::Add(): epoll_ctl() failed");
	}
#elif M_OS == M_OS_MACOSX
	ASSERT(this->size() <= this->revents.size() / 2)
	
	if(wait_for.get(ready::read)){
		this->add_filter(w, EVFILT_READ);
	}
	if(wait_for.get(ready::write)){
		this->add_filter(w, EVFILT_WRITE);
	}
#else
#	error "Unsupported OS"
#endif

	++this->size_of_wait_set;

	w.is_added_to_waitset = true;
//		TRACE(<< "wait_set::add(): exit" << std::endl)
}

void wait_set::change(waitable& w, utki::flags<ready> wait_for){
	ASSERT(w.is_added())

#if M_OS == M_OS_WINDOWS
	// check if the waitable object is added to this wait set
	{
		unsigned i;
		for(i = 0; i < this->size(); ++i){
			if(this->waitables[i] == &w){
				break;
			}
		}
		ASSERT(i <= this->size())
		if(i == this->size()){
			throw utki::invalid_state("wait_set::change(): the waitable is not added to this wait set");
		}
	}

	// set new wait flags
	w.set_waiting_flags(wait_for);

#elif M_OS == M_OS_LINUX
	epoll_event e;
	e.data.fd = w.get_handle();
	e.data.ptr = &w;
	e.events =
			(wait_for.get(ready::read) ? (EPOLLIN | EPOLLPRI) : 0)
			| (wait_for.get(ready::write) ? EPOLLOUT : 0)
			| (EPOLLERR);
	int res = epoll_ctl(
			this->epollSet,
			EPOLL_CTL_MOD,
			w.get_handle(),
			&e
		);
	if(res < 0){
		throw std::system_error(errno, std::generic_category(), "wait_set::change(): epoll_ctl() failed");
	}
#elif M_OS == M_OS_MACOSX
	if(wait_for.get(ready::read)){
		this->add_filter(w, EVFILT_READ);
	}else{
		this->remove_filter(w, EVFILT_READ);
	}
	if(wait_for.get(ready::write)){
		this->add_filter(w, EVFILT_WRITE);
	}else{
		this->remove_filter(w, EVFILT_WRITE);
	}
#else
#	error "Unsupported OS"
#endif
}

void wait_set::remove(waitable& w)noexcept{
	ASSERT(w.is_added())
	
	ASSERT(this->size() != 0)

#if M_OS == M_OS_WINDOWS
	// remove object from array
	{
		unsigned i;
		for(i = 0; i < this->size_of_wait_set; ++i){
			if(this->waitables[i] == &w){
				break;
			}
		}
		ASSERT(i <= this->size_of_wait_set)
		ASSERT_INFO(i != this->size_of_wait_set, "wait_set::remove(): waitable is not added to wait set")

		unsigned numObjects = this->size_of_wait_set - 1; // decrease number of objects before shifting the object handles in the array
		// shift object handles in the array
		for(; i < numObjects; ++i){
			this->handles[i] = this->handles[i + 1];
			this->waitables[i] = this->waitables[i + 1];
		}
	}

	// clear wait flags (deassociate socket and Windows event)
	w.set_waiting_flags(false);

#elif M_OS == M_OS_LINUX
	int res = epoll_ctl(
			this->epollSet,
			EPOLL_CTL_DEL,
			w.get_handle(),
			0
		);
	if(res < 0){
		ASSERT_INFO(false, "wait_set::Remove(): epoll_ctl failed, probably the waitable was not added to the wait set")
	}
#elif M_OS == M_OS_MACOSX	
	this->remove_filter(w, EVFILT_READ);
	this->remove_filter(w, EVFILT_WRITE);
#else
#	error "Unsupported OS"
#endif

	--this->size_of_wait_set;

	w.is_added_to_waitset = false;
//		TRACE(<< "wait_set::remove(): completed successfuly" << std::endl)
}

unsigned wait_set::wait_internal_linux(int timeout, utki::span<waitable*> out_events){
	// TRACE(<< "going to epoll_wait() with timeout = " << timeout << std::endl)

	int res;

	while(true){
		res = epoll_wait(
				this->epollSet,
				this->revents.data(),
				this->revents.size(),
				timeout
			);

		// TRACE(<< "epoll_wait() returned " << res << std::endl)

		if(res < 0){
			// if interrupted by signal, try waiting again.
			if(errno == EINTR){
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "wait_set::wait(): epoll_wait() failed");
		}
		break;
	};

	ASSERT(res >= 0)
	ASSERT(unsigned(res) <= this->revents.size())

	unsigned numEvents = 0;
	for(
			epoll_event *e = this->revents.data();
			e < this->revents.data() + res;
			++e
		)
	{
		waitable* w = static_cast<waitable*>(e->data.ptr);
		ASSERT(w)
		if((e->events & EPOLLERR) != 0){
			w->readiness_flags.set(ready::error);
		}
		if((e->events & (EPOLLIN | EPOLLPRI)) != 0){
			w->readiness_flags.set(ready::read);
		}
		if((e->events & EPOLLOUT) != 0){
			w->readiness_flags.set(ready::write);
		}
		ASSERT(!w->readiness_flags.is_clear())
		if(numEvents < out_events.size()){
			out_events[numEvents] = w;
			++numEvents;
		}
	}

	ASSERT(res >= 0) // NOTE: 'res' can be zero, if no events happened in the specified timeout
	return unsigned(res);
}

unsigned wait_set::wait_internal(bool waitInfinitly, uint32_t timeout, utki::span<waitable*> out_events){
	if(this->size_of_wait_set == 0){
		throw std::logic_error("wait_set::wait(): no waitable objects were added to the wait_set, can't perform wait()");
	}

#if M_OS == M_OS_WINDOWS
	DWORD waitTimeout;
	if(waitInfinitly){
		waitTimeout = INFINITE;
	}else{
		static_assert(INFINITE == 0xffffffff, "check that INFINITE macro is max uint32_T failed");
		if(timeout == 0xffffffff){
			waitTimeout = 0xffffffff - 1;
		}else{
			waitTimeout = DWORD(timeout);
		}
	}

	DWORD res = WaitForMultipleObjectsEx(
			this->size_of_wait_set,
			this->handles.data(),
			FALSE, // do not wait for all objects, wait for at least one
			waitTimeout,
			FALSE
		);

	ASSERT(res != WAIT_IO_COMPLETION) // it is impossible because we supplied FALSE as last parameter to WaitForMultipleObjectsEx()

	// we are not expecting abandoned mutexes
	ASSERT(res < WAIT_ABANDONED_0 || (WAIT_ABANDONED_0 + this->size_of_wait_set) <= res)

	if(res == WAIT_FAILED){
		throw std::system_error(GetLastError(), std::generic_category(), "wait_set::wait(): WaitForMultipleObjectsEx() failed");
	}

	if(res == WAIT_TIMEOUT){
		return 0;
	}

	ASSERT(WAIT_OBJECT_0 <= res && res < (WAIT_OBJECT_0 + this->size_of_wait_set ))

	// check for activities
	unsigned numEvents = 0;
	for(unsigned i = 0; i < this->size_of_wait_set; ++i){
		if(this->waitables[i]->check_signaled()){
			if(numEvents < out_events.size()){
				out_events[numEvents] = this->waitables[i];
			}
			++numEvents;
		}else{
			// NOTE: sometimes the event is reported as signaled, but no read/write events indicated.
			//       Don't know why it happens.
//			ASSERT_INFO(i != (res - WAIT_OBJECT_0), "i = " << i << " (res - WAIT_OBJECT_0) = " << (res - WAIT_OBJECT_0) << " wait_flags = " << this->waitables[i]->readiness_flags)
		}
	}

	// NOTE: Sometimes the event is reported as signaled, but no actual activity is there.
	//       Don't know why.
//		ASSERT(numEvents > 0)

	return numEvents;

#elif M_OS == M_OS_LINUX
	if(waitInfinitly){
		return this->wait_internal_linux(-1, out_events);
	}

	// in linux, epoll_wait() gets timeout as int argument, while we have timeout as uint32_t,
	// so the requested timeout can be bigger than int can hold (negative values of the int are not used)

	uint32_t max_time_step = uint32_t(std::numeric_limits<int>::max());

	while(timeout >= max_time_step){
		ASSERT_INFO(int(max_time_step) >= 0, "timeout = 0x" << std::hex << timeout)
		auto res = this->wait_internal_linux(max_time_step, out_events);
		if(res != 0){
			return res;
		}
		timeout -= max_time_step;
		if(timeout == 0){
			return 0;
		}
	}

	return this->wait_internal_linux(timeout, out_events);
	
#elif M_OS == M_OS_MACOSX
	struct timespec ts = {
		decltype(timespec::tv_sec)(timeout / 1000), // seconds
		decltype(timespec::tv_nsec)((timeout % 1000) * 1000000) // nanoseconds
	};

	for(;;){
		int res = kevent(
				this->queue,
				0,
				0,
				this->revents.data(),
				int(this->revents.size()),
				(waitInfinitly) ? 0 : &ts
			);

		if(res < 0){
			if(errno == EINTR){
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "wait_set::wait(): kevent() failed");
		}else if(res == 0){
			return 0; // timeout
		}else if(res > 0){
			unsigned out_i = 0; // index into out_events
			for(unsigned i = 0; i != unsigned(res); ++i){
				struct kevent &e = this->revents[i];
				waitable *w = reinterpret_cast<waitable*>(e.udata);
				if(e.filter == EVFILT_WRITE){
					w->readiness_flags.set(ready::write);
				}else if(e.filter == EVFILT_READ){
					w->readiness_flags.set(ready::read);
				}
				
				if((e.flags & EV_ERROR) != 0){
					w->readiness_flags.set(ready::error);
				}
				
				if(out_i < out_events.size()){
					// check if waitable is already added
					unsigned k = 0;
					for(; k != out_i; ++k){
						if(out_events[k] == w){
							break;
						}
					}
					if(k == out_i){
						out_events[out_i] = w;
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
