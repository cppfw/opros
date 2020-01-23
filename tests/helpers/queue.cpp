#include "queue.hpp"

#include <mutex>
#include <unistd.h>

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
	//can write will always be set because it is always possible to post a message to the queue
	this->setCanWriteFlag();

#if M_OS == M_OS_WINDOWS
	this->eventForWaitable = CreateEvent(
			NULL, //security attributes
			TRUE, //manual-reset
			FALSE, //not signalled initially
			NULL //no name
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
		//Set CanRead flag.
		//NOTE: in linux implementation with epoll(), the CanRead
		//flag will also be set in WaitSet::Wait() method.
		//NOTE: set CanRead flag before event notification/pipe write, because
		//if do it after then some other thread which was waiting on the WaitSet
		//may read the CanRead flag while it was not set yet.
		ASSERT(!this->canRead())
		this->setCanReadFlag();

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

	ASSERT(this->canRead())
}



queue::T_Message queue::peekMsg(){
	std::lock_guard<decltype(this->mut)> mutexGuard(this->mut);
	if(this->messages.size() != 0){
		ASSERT(this->canRead())

		if(this->messages.size() == 1){//if we are taking away the last message from the queue
#if M_OS == M_OS_WINDOWS
			if(ResetEvent(this->eventForWaitable) == 0){
				ASSERT(false)
				throw std::system_error(GetLastError(), std::generic_category(), "queue::Wait(): ResetEvent() failed");
			}
#elif M_OS == M_OS_MACOSX
			{
				std::uint8_t oneByteBuf[1];
				if(read(this->pipeEnds[0], oneByteBuf, 1) != 1){
					throw std::system_error(errno, std::generic_category(), "queue::Wait(): read() failed");
				}
			}
#elif M_OS == M_OS_LINUX
			{
				eventfd_t value;
				if(eventfd_read(this->eventFD, &value) < 0){
					throw std::system_error(errno, std::generic_category(), "queue::Wait(): eventfd_read() failed");
				}
				ASSERT(value == 1)
			}
#else
#	error "Unsupported OS"
#endif
			this->clearCanReadFlag();
		}else{
			ASSERT(this->canRead())
		}
		
		T_Message ret = std::move(this->messages.front());
		
		this->messages.pop_front();
		
		return ret;
	}
	return nullptr;
}



#if M_OS == M_OS_WINDOWS
HANDLE queue::getHandle(){
	//return event handle
	return this->eventForWaitable;
}



void queue::setWaitingEvents(std::uint32_t flagsToWaitFor){
	//It is not allowed to wait on queue for write,
	//because it is always possible to push new message to queue.
	//Error condition is not possible for queue.
	//Thus, only possible flag values are READ and 0 (NOT_READY)
	if(flagsToWaitFor != 0 && flagsToWaitFor != pogodi::Waitable::READ){
		ASSERT_INFO(false, "flagsToWaitFor = " << flagsToWaitFor)
		throw std::invalid_argument("queue::SetWaitingEvents(): flagsToWaitFor should be pogodi::Waitable::READ or 0, other values are not allowed");
	}

	this->flagsMask = flagsToWaitFor;
}



//returns true if signaled
bool queue::checkSignaled(){
	//error condition is not possible for queue
	ASSERT((this->readinessFlags & pogodi::Waitable::ERROR_CONDITION) == 0)

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

	return (this->readinessFlags & this->flagsMask) != 0;
}

#elif M_OS == M_OS_MACOSX
//override
int queue::getHandle(){
	//return read end of pipe
	return this->pipeEnds[0];
}

#elif M_OS == M_OS_LINUX
//override
int queue::getHandle(){
	return this->eventFD;
}

#else
#	error "Unsupported OS"
#endif
