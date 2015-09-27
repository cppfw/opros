#pragma once

#include <cstdint>

#include <utki/config.hpp>
#include <utki/debug.hpp>

namespace wt{

/**
 * @brief Base class for objects which can be waited for.
 * Base class for objects which can be used in wait sets.
 */
class Waitable{
	friend class WaitSet;

	bool isAdded_var = false;

	void* userData = nullptr;

public:
	enum EReadinessFlags{
		NOT_READY = 0,      // bin: 00000000
		READ = 1,           // bin: 00000001
		WRITE = 2,          // bin: 00000010
		READ_AND_WRITE = 3, // bin: 00000011
		ERROR_CONDITION = 4 // bin: 00000100
	};

protected:
	std::uint32_t readinessFlags = NOT_READY;

	Waitable() = default;



	bool isAdded()const noexcept{
		return this->isAdded_var;
	}




	Waitable(const Waitable& w) = delete;
	
	Waitable(Waitable&& w);


	Waitable& operator=(Waitable&& w);



	void setCanReadFlag()noexcept{
		this->readinessFlags |= READ;
	}

	void clearCanReadFlag()noexcept{
		this->readinessFlags &= (~READ);
	}

	void setCanWriteFlag()noexcept{
		this->readinessFlags |= WRITE;
	}

	void clearCanWriteFlag()noexcept{
		this->readinessFlags &= (~WRITE);
	}

	void setErrorFlag()noexcept{
		this->readinessFlags |= ERROR_CONDITION;
	}

	void clearErrorFlag()noexcept{
		this->readinessFlags &= (~ERROR_CONDITION);
	}

	void clearAllReadinessFlags()noexcept{
		this->readinessFlags = NOT_READY;
	}

public:
	virtual ~Waitable()noexcept{
		ASSERT(!this->isAdded_var)
	}

	/**
	 * @brief Check if "Can read" flag is set.
	 * @return true if Waitable is ready for reading.
	 */
	bool canRead()const noexcept{
		return (this->readinessFlags & READ) != 0;
	}

	/**
	 * @brief Check if "Can write" flag is set.
	 * @return true if Waitable is ready for writing.
	 */
	bool canWrite()const noexcept{
		return (this->readinessFlags & WRITE) != 0;
	}

	/**
	 * @brief Check if "error" flag is set.
	 * @return true if Waitable is in error state.
	 */
	bool errorCondition()const noexcept{
		return (this->readinessFlags & ERROR_CONDITION) != 0;
	}

	/**
	 * @brief Get user data associated with this Waitable.
	 * Returns the pointer to the user data which was previously set by SetUserData() method.
	 * @return pointer to the user data.
	 * @return zero pointer if the user data was not set.
	 */
	void* getUserData()noexcept{
		return this->userData;
	}

	/**
	 * @brief Set user data.
	 * See description of GetUserData() for more details.
	 * @param data - pointer to the user data to associate with this Waitable.
	 */
	void setUserData(void* data)noexcept{
		this->userData = data;
	}

#if M_OS == M_OS_WINDOWS
protected:
	virtual HANDLE getHandle() = 0;

	virtual void setWaitingEvents(std::uint32_t /*flagsToWaitFor*/){}

	//returns true if signaled
	virtual bool checkSignaled(){
		return this->readinessFlags != 0;
	}

#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
public:
	/**
	 * @brief Get Unix file descriptor.
	 * This method is specific to Unix-based operating systems, like Linux, MAC OS X, Unix.
	 * This method is made public in order to ease embedding Waitables to existing epoll() sets.
	 * Use this method only if you know what you are doing!
	 */
	virtual int getHandle() = 0;

#else
#	error "Unsupported OS"
#endif

};

}
