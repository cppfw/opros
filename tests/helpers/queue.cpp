#include "queue.hpp"

#include <mutex>

#if CFG_OS != CFG_OS_WINDOWS
#	include <unistd.h>
#endif

#if CFG_OS == CFG_OS_LINUX
#	include <sys/eventfd.h>
#	include <cstring>
#endif

using namespace helpers;

queue::queue():
	opros::waitable(
		[
#if CFG_OS == CFG_OS_MACOSX
			this
#endif
		](){
#if CFG_OS == CFG_OS_WINDOWS
	this->eventForWaitable = CreateEvent(
			NULL, // security attributes
			TRUE, // manual-reset
			FALSE, // not signalled initially
			NULL // no name
		);
	if(this->eventForWaitable == NULL){
		throw std::system_error(GetLastError(), std::generic_category(), "could not create event (Win32) for implementing Waitable");
	}
#elif CFG_OS == CFG_OS_MACOSX
	int ends[2];
	if(::pipe(&ends[0]) < 0){
		throw std::system_error(errno, std::generic_category(), "could not create pipe (*nix) for implementing Waitable");
	}
	this->pipe_end = ends[1];
	return ends[0];
#elif CFG_OS == CFG_OS_LINUX
	int event_fd = eventfd(0, EFD_NONBLOCK);
	if(event_fd < 0){
		throw std::system_error(errno, std::generic_category(), "could not create eventfd (linux) for implementing Waitable");
	}
	return event_fd;
#else
#	error "Unsupported OS"
#endif
		}()
	)
{}

queue::~queue()noexcept{
#if CFG_OS == CFG_OS_WINDOWS
	CloseHandle(this->eventForWaitable);
#elif CFG_OS == CFG_OS_MACOSX
	close(this->handle);
	close(this->pipe_end);
#elif CFG_OS == CFG_OS_LINUX
	close(this->handle);
#else
#	error "Unsupported OS"
#endif
}

void queue::push_message(std::function<void()>&& msg)noexcept{
	std::lock_guard<decltype(this->mut)> mutex_guard(this->mut);
	this->messages.push_back(std::move(msg));
	
	if(this->messages.size() == 1){ // if it is a first message
#if CFG_OS == CFG_OS_WINDOWS
		if(SetEvent(this->eventForWaitable) == 0){
			ASSERT(false)
		}
#elif CFG_OS == CFG_OS_MACOSX
		{
			std::uint8_t one_byte_buf[1];
			if(write(this->pipe_end, one_byte_buf, 1) != 1){
				ASSERT(false)
			}
		}
#elif CFG_OS == CFG_OS_LINUX
		if(eventfd_write(this->handle, 1) < 0){
			ASSERT(false)
		}
#else
#	error "Unsupported OS"
#endif
	}
}

queue::message_type queue::peek_msg(){
	std::lock_guard<decltype(this->mut)> mutex_guard(this->mut);
	if(this->messages.size() != 0){
		if(this->messages.size() == 1){ // if we are taking away the last message from the queue
#if CFG_OS == CFG_OS_WINDOWS
			if(ResetEvent(this->eventForWaitable) == 0){
				ASSERT(false)
				throw std::system_error(GetLastError(), std::generic_category(), "queue::wait(): ResetEvent() failed");
			}
#elif CFG_OS == CFG_OS_MACOSX
			{
				std::uint8_t one_byte_buf[1];
				if(read(this->handle, one_byte_buf, 1) != 1){
					throw std::system_error(errno, std::generic_category(), "queue::wait(): read() failed");
				}
			}
#elif CFG_OS == CFG_OS_LINUX
			{
				eventfd_t value;
				if(eventfd_read(this->handle, &value) < 0){
					throw std::system_error(errno, std::generic_category(), "queue::wait(): eventfd_read() failed");
				}
				ASSERT(value == 1)
			}
#else
#	error "Unsupported OS"
#endif
		}
		
		message_type ret = std::move(this->messages.front());
		
		this->messages.pop_front();
		
		return ret;
	}
	return nullptr;
}

#if CFG_OS == CFG_OS_WINDOWS
HANDLE queue::get_handle(){
	return this->eventForWaitable;
}

void queue::set_waiting_flags(utki::flags<opros::ready> wait_for){
	// It is not allowed to wait on queue for write,
	// because it is always possible to push new message to queue.
	// Error condition is not possible for queue.
	// Thus, only possible flag values are READ and 0 (NOT_READY)
	if(wait_for.get(opros::ready::write)){
		ASSERT(false, [&](auto&o){o << "wait_for = " << wait_for;})
		throw std::invalid_argument("queue::set_waiting_flags(): wait_for should have only ready::read flag set or no flags set, other values are not allowed");
	}

	this->flagsMask = wait_for;
}

bool queue::check_signaled(){
	// error condition is not possible for queue
	ASSERT(!this->readiness_flags.get(opros::ready::error))

	return !(this->readiness_flags & this->flagsMask).is_clear();
}
#endif
