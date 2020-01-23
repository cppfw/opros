#pragma once

#include <vector>
#include <sstream>
#include <cerrno>
#include <cstdint>
#include <stdexcept>

#include <utki/config.hpp>
#include <utki/debug.hpp>
#include <utki/span.hpp>


#if M_OS == M_OS_WINDOWS
#	include <utki/windows.hpp>

#elif M_OS == M_OS_LINUX
#	include <sys/epoll.h>
#	include <unistd.h>

#elif M_OS == M_OS_MACOSX
#	include <sys/types.h>
#	include <sys/event.h>
#	include <unistd.h>

#else
#	error "Unsupported OS"
#endif


#include "waitable.hpp"


namespace opros{

/**
 * @brief Set of waitable objects to wait for.
 */
class wait_set{
	const unsigned max_size_of_wait_set;
	unsigned size_of_wait_set = 0;

#if M_OS == M_OS_WINDOWS
	std::vector<waitable*> waitables;
	std::vector<HANDLE> handles; // used to pass array of HANDLEs to WaitForMultipleObjectsEx()

#elif M_OS == M_OS_LINUX
	int epollSet;

	std::vector<epoll_event> revents; // used for getting the result from epoll_wait()
#elif M_OS == M_OS_MACOSX
	int queue; // kqueue
	
	std::vector<struct kevent> revents; // used for getting the result
#else
#	error "Unsupported OS"
#endif

public:
	/**
	 * @brief Constructor.
	 * @param max_size - maximum number of waitable objects that can be added to the wait set.
	 */
	wait_set(unsigned max_size) :
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
		ASSERT(int(max_size) > 0)
		// TODO: check that unsigned fits to int and throw exception if not
		this->epollSet = epoll_create(int(max_size));
		if(this->epollSet < 0){
			throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): epoll_create() failed");
		}
	}
#elif M_OS == M_OS_MACOSX
	{
		//TODO: chack that max_size * 2 fits to size type of revents
		this->revents.resize(max_size * 2);
		this->queue = kqueue();
		if(this->queue == -1){
			throw std::system_error(errno, std::generic_category(), "wait_set::wait_set(): kqueue creation failed");
		}
	}
#else
#	error "Unsupported OS"
#endif



	/**
	 * @brief Destructor.
	 * Note, that destructor will check if the wait set is empty. If it is not, then an assert
	 * will be triggered.
	 * It is user's responsibility to remove any waitable objects from the waitset
	 * before the wait set object is destroyed.
	 */
	~wait_set()noexcept{
		ASSERT_INFO(this->size_of_wait_set == 0, "attempt to destroy wait_set containig waitables")
#if M_OS == M_OS_WINDOWS
		// do nothing
#elif M_OS == M_OS_LINUX
		close(this->epollSet);
#elif M_OS == M_OS_MACOSX
		close(this->queue);
#else
#	error "Unsupported OS"
#endif
	}



	/**
	 * @brief Get maximum size of the wait set.
	 * @return maximum number of waitables this wait_set can hold.
	 */
	unsigned max_size()const noexcept{
		return this->max_size_of_wait_set;
	}

	/**
	 * @brief Get number of waitables already added to the wait_set.
	 * @return number of waitables added to th wait_set.
	 */
	unsigned size()const noexcept{
		return this->size_of_wait_set;
	}

	/**
	 * @brief Add waitable object to the wait set.
	 * @param w - waitable object to add to the wait_set.
	 * @param wait_for - determine events waiting for which we are interested.
	 */
	void add(waitable& w, utki::flags<ready_to> wait_for);

	/**
	 * @brief Change wait flags for a given waitable.
	 * Changes wait flags for a given waitable, which is in this wait_set.
	 * @param w - waitable for which the changing of wait flags is needed.
	 * @param wait_for - new wait flags to be set for the given waitable.
	 */
	void change(waitable& w, utki::flags<ready_to> wait_for);

	/**
	 * @brief Remove waitable from wait set.
	 * @param w - waitable object to be removed from the wait_set.
	 */
	void remove(waitable& w)noexcept;

	/**
	 * @brief wait for event.
	 * This function blocks calling thread execution until one of the waitable objects in the wait_set
	 * triggers. Upon return from the function, pointers to triggered objects are placed in the
	 * 'out_events' buffer and the return value from the function indicates number of these objects
	 * which have triggered.
	 * Note, that it does not change the readiness state of non-triggered objects.
	 * @param out_events - pointer to buffer where to put pointers to triggered waitable objects.
	 *                     The buffer size must be equal or greater than the number of waitables
	 *                     currently added to the wait set, otherwise not all triggered waitables
	 *                     will be reported to this buffer.
	 * @return number of objects triggered.
	 *         NOTE: for some reason, on Windows it can return 0 objects triggered.
	 */
	unsigned wait(utki::span<waitable*> out_events = utki::span<waitable*>(nullptr, 0)){
		return this->wait_internal(true, 0, out_events);
	}

	/**
	 * @brief wait for event with timeout.
	 * The same as wait() function, but takes wait timeout as parameter. Thus,
	 * this function will wait for any event or until timeout is hit. Note, that it guarantees that
	 * it will wait AT LEAST for specified number of milliseconds.
	 * @param timeout - maximum time in milliseconds to wait.
	 * @param out_events - buffer where to put pointers to triggered waitable objects.
	 *                     The buffer size must be equal or greater than the number of waitables
	 *                     currently added to the wait set, otherwise not all triggered waitables
	 *                     will be reported to this buffer.
	 * @return number of objects triggered. If 0 then timeout was hit.
	 *         NOTE: for some reason, on Windows it can return 0 before timeout was hit.
	 */
	unsigned wait(uint32_t timeout, utki::span<waitable*> out_events = utki::span<waitable*>(nullptr, 0)){
		return this->wait_internal(false, timeout, out_events);
	}

	//TODO: deprecated, remove.
	unsigned waitWithTimeout(uint32_t timeout, utki::span<waitable*> out_events){
		return this->wait_internal(false, timeout, out_events);
	}

private:
	unsigned wait_internal(bool waitInfinitly, std::uint32_t timeout, utki::span<waitable*> out_events);
	
	
#if M_OS == M_OS_MACOSX
	void add_filter(waitable& w, int16_t filter);
	void remove_filter(waitable& w, int16_t filter);
#endif

};

// TODO: deprecated, remove.
typedef wait_set WaitSet;

}

// TODO: deprecated, remove.
namespace pogodi = opros;
