#include "queue.hpp"

#include <mutex>

#if M_OS != M_OS_WINDOWS
#	include <unistd.h>
#endif

#if M_OS == M_OS_LINUX
#	include <sys/eventfd.h>
#	include <cstring>
#endif


//#define M_ENABLE_QUEUE_TRACE
#ifdef M_ENABLE_QUEUE_TRACE
#	define M_QUEUE_TRACE(x) TRACE(<< "[QUEUE] ") TRACE(x)
#else
#	define M_QUEUE_TRACE(x)
#endif




using namespace helpers;



queue::queue(){
	// can write will always be set because it is always possible to post a message to the queue
	this->readiness_flags.set(opros::ready::write);

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
	this->eventFD = eventfd(0, EFD_NONBLOCK);
	if(this->eventFD < 0){
		throw std::system_error(errno, std::generic_category(), "could not create eventfd (linux) for implementing Waitable");
	}
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
	close(this->eventFD);
#else
#	error "Unsupported OS"
#endif
}



void queue::pushMessage(std::function<void()>&& msg)noexcept{
	std::lock_guard<decltype(this->mut)> mutexGuard(this->mut);
	this->messages.push_back(std::move(msg));
	
	if(this->messages.size() == 1){//if it is a first message
		// Set CanRead flag.
		// NOTE: in linux implementation with epoll(), the CanRead
		// flag will also be set in WaitSet::Wait() method.
		// NOTE: set CanRead flag before event notification/pipe write, because
		// if do it after then some other thread which was waiting on the WaitSet
		// may read the CanRead flag while it was not set yet.
		ASSERT(!this->readiness_flags.get(opros::ready::read))
		this->readiness_flags.set(opros::ready::read);

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
		if(eventfd_write(this->eventFD, 1) < 0){
			ASSERT(false)
		}
#else
#	error "Unsupported OS"
#endif
	}

	ASSERT(this->readiness_flags.get(opros::ready::read))
}



queue::T_Message queue::peekMsg(){
	std::lock_guard<decltype(this->mut)> mutexGuard(this->mut);
	if(this->messages.size() != 0){
		ASSERT(this->readiness_flags.get(opros::ready::read))

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
				if(eventfd_read(this->eventFD, &value) < 0){
					throw std::system_error(errno, std::generic_category(), "queue::wait(): eventfd_read() failed");
				}
				ASSERT(value == 1)
			}
#else
#	error "Unsupported OS"
#endif
			this->readiness_flags.clear(opros::ready::read);
		}else{
			ASSERT(this->readiness_flags.get(opros::ready::read))
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
		ASSERT_INFO(false, "wait_for = " << wait_for)
		throw std::invalid_argument("queue::set_waiting_flags(): wait_for should have only ready::read flag set or no flags set, other values are not allowed");
	}

	this->flagsMask = wait_for;
}

bool queue::check_signaled(){
	// error condition is not possible for queue
	ASSERT(!this->readiness_flags.get(opros::ready::error))

// TODO: remove dead code
/*
#ifdef DEBUG
	{
		atomic::SpinLock::GuardYield mutexGuard(this->mut);
		if(this->first){
			ASSERT_ALWAYS(this->CanRead())

			//event should be in signalled state
			ASSERT_ALWAYS(WaitForSingleObject(this->eventForWaitable, 0) == WAIT_OBJECT_0)
		}

		if(this->CanRead()){
			ASSERT_ALWAYS(this->first)

			//event should be in signalled state
			ASSERT_ALWAYS(WaitForSingleObject(this->eventForWaitable, 0) == WAIT_OBJECT_0)
		}

		//if event is in signalled state
		if(WaitForSingleObject(this->eventForWaitable, 0) == WAIT_OBJECT_0){
			ASSERT_ALWAYS(this->CanRead())
			ASSERT_ALWAYS(this->first)
		}
	}
#endif
*/

	return !(this->readiness_flags & this->flagsMask).is_clear();
}

#elif M_OS == M_OS_MACOSX
int queue::get_handle(){
	// return read end of pipe
	return this->pipeEnds[0];
}

#elif M_OS == M_OS_LINUX
int queue::get_handle(){
	return this->eventFD;
}

#else
#	error "Unsupported OS"
#endif
