#pragma once

#include <cstdint>

#include <utki/config.hpp>
#include <utki/debug.hpp>
#include <utki/flags.hpp>

#if M_OS == M_OS_WINDOWS
#	include <utki/windows.hpp>
#endif

namespace opros{

enum class ready{
	read,
	write,
	error,

	enum_size // this must always be the last element of the enum
};

/**
 * @brief Base class for objects which can be waited for.
 * Base class for objects which can be used in wait sets.
 */
class waitable{
	friend class wait_set;

	bool is_added_to_waitset = false;

public:
	/**
	 * @brief User data assotiated with the waitable.
	 */
	void* user_data = nullptr;

protected:
	utki::flags<ready> readiness_flags;

	waitable() = default;

	waitable(const waitable& w) = delete;
	waitable& operator=(const waitable& w) = delete;
	
	waitable(waitable&& w);
	waitable& operator=(waitable&& w);

	bool is_added()const noexcept{
		return this->is_added_to_waitset;
	}

public:
	virtual ~waitable()noexcept{
		ASSERT_INFO(!this->is_added(), "~waitable(): the waitable is currently added to some wait_set()")
	}

	const decltype(readiness_flags)& flags()const noexcept{
		return this->readiness_flags;
	}

#if M_OS == M_OS_WINDOWS
protected:
	virtual HANDLE get_handle() = 0;

	virtual void set_waiting_flags(utki::flags<ready>){}

	virtual bool check_signaled(){
		return !this->readiness_flags.is_clear();
	}

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
public:
	/**
	 * @brief Get Unix file descriptor.
	 * This method is specific to Unix-based operating systems, like Linux, MAC OS X, Unix.
	 * This method is made public in order to ease embedding waitables to existing epoll() sets.
	 */
	virtual int get_handle() = 0;

#else
#	error "Unsupported OS"
#endif

};

}
