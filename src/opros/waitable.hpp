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

#pragma once

#include <cstdint>

#include <utki/config.hpp>
#include <utki/debug.hpp>
#include <utki/flags.hpp>

#if CFG_OS == CFG_OS_WINDOWS
#	include <utki/windows.hpp>
#endif

namespace opros {

/**
 * @brief Readiness flags.
 */
enum class ready {
	/**
	 * @brief Flag indicating ready to read.
	 */
	read,

	/**
	 * @brief Flag indicating ready to write.
	 */
	write,

	/**
	 * @brief Flag indicating error state.
	 */
	error,

	enum_size // this must always be the last element of the enum
};

/**
 * @brief Base class for objects which can be waited for.
 * Base class for objects which can be used in wait sets.
 */
class waitable
{
	friend class wait_set;

public:
	/**
	 * @brief User data assotiated with the waitable.
	 */
	void* user_data = nullptr;

	waitable(const waitable& w) = delete;
	waitable& operator=(const waitable& w) = delete;

protected:
#if CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX
	waitable(int handle) :
		handle(handle)
	{}
#elif CFG_OS == CFG_OS_WINDOWS
	waitable(HANDLE handle) :
		handle(handle)
	{}
#else
#	error "Unknown OS"
#endif

	// Destructor is protected because this class is supposed to be used as a base
	// class, but is not supposed to be destroyed via base pointer.
	// TODO: is it possible to check it with static_assert? if so, add test and
	// move this note there
#if CFG_OS == CFG_OS_WINDOWS
	virtual
#endif
		~waitable() = default;

#if CFG_OS == CFG_OS_WINDOWS

protected:
	HANDLE handle;

	virtual void set_waiting_flags(utki::flags<ready>) = 0;
	virtual utki::flags<ready> get_readiness_flags() = 0;

#elif CFG_OS == CFG_OS_LINUX || CFG_OS == CFG_OS_MACOSX || CFG_OS == CFG_OS_UNIX

protected:
	int handle;

#else
#	error "Unsupported OS"
#endif
};

} // namespace opros
