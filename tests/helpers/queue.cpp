#include "queue.hpp"

#include <mutex>

#if M_OS != M_OS_WINDOWS
#	include <unistd.h>
#endif

#if M_OS == M_OS_LINUX
#	include <sys/eventfd.h>
#	include <cstring>
#endif


using namespace helpers;



queue::queue():
	opros::waitable(
		[](){
#if M_OS == M_OS_WINDOWS
	this->eventForWaitable = CreateEvent(
			NULL, // security attributes
			TRUE, // manual-reset
			FALSE, // not signalled initially
			NULL // no name
		);
	if(this->eventForWaitable == NULL){
		throw std::system_error(GetLastError(), std::generic_category(), "could not create event (Win32) for implementing Waitable");
	}
#elif M_OS == M_OS_MACOSX
	if(::pipe(&this->pipeEnds[0]) < 0){
		throw std::system_error(errno, std::generic_category(), "could not create pipe (*nix) for implementing Waitable");
	}
#elif M_OS == M_OS_LINUX
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
{
#if M_OS == M_OS_WINDOWS
	this->eventForWaitable = CreateEvent(
			NULL, // security attributes
			TRUE, // manual-reset
			FALSE, // not signalled initially
			NULL // no name
		);
	if(this->eventForWaitable == NULL){
		throw std::system_error(GetLastError(), std::generic_category(), "could not create event (Win32) for implementing Waitable");
	}
#elif M_OS == M_OS_MACOSX
	if(::pipe(&this->pipeEnds[0]) < 0){
		throw std::system_error(errno, std::generic_category(), "could not create pipe (*nix) for implementing Waitable");
	}
#elif M_OS == M_OS_LINUX
#else
#	error "Unsupported OS"
#endif
}



queue::~queue()noexcept{
#if M_OS == M_OS_WINDOWS
	CloseHandle(this->eventForWaitable);
#elif M_OS == M_OS_MACOSX
	close(this->pipeEnds[0]);
	close(this->pipeEnds[1]);
#elif M_OS == M_OS_LINUX
	close(this->handle);
#else
#	error "Unsupported OS"
#endif
}



void queue::pushMessage(std::function<void()>&& msg)noexcept{
	std::lock_guard<decltype(this->mut)> mutexGuard(this->mut);
	this->messages.push_back(std::move(msg));
	
	if(this->messages.size() == 1){ // if it is a first message
#if M_OS == M_OS_WINDOWS
		if(SetEvent(this->eventForWaitable) == 0){
			ASSERT(false)
		}
#elif M_OS == M_OS_MACOSX
		{
			std::uint8_t oneByteBuf[1];
			if(write(this->pipeEnds[1], oneByteBuf, 1) != 1){
				ASSERT(false)
			}
		}
#elif M_OS == M_OS_LINUX
		if(eventfd_write(this->handle, 1) < 0){
			ASSERT(false)
		}
#else
#	error "Unsupported OS"
#endif
	}
}



queue::T_Message queue::peekMsg(){
	std::lock_guard<decltype(this->mut)> mutexGuard(this->mut);
	if(this->messages.size() != 0){
		if(this->messages.size() == 1){ // if we are taking away the last message from the queue
#if M_OS == M_OS_WINDOWS
			if(ResetEvent(this->eventForWaitable) == 0){
				ASSERT(false)
				throw std::system_error(GetLastError(), std::generic_category(), "queue::wait(): ResetEvent() failed");
			}
#elif M_OS == M_OS_MACOSX
			{
				std::uint8_t oneByteBuf[1];
				if(read(this->pipeEnds[0], oneByteBuf, 1) != 1){
					throw std::system_error(errno, std::generic_category(), "queue::wait(): read() failed");
				}
			}
#elif M_OS == M_OS_LINUX
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
		
		T_Message ret = std::move(this->messages.front());
		
		this->messages.pop_front();
		
		return ret;
	}
	return nullptr;
}



#if M_OS == M_OS_WINDOWS
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
