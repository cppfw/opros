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

#if M_OS == M_OS_WINDOWS
#	include <utki/windows.hpp>
#endif

namespace opros {

enum class ready {
	read,
	write,
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

	// TODO: remove lint suppression when https://github.com/llvm/llvm-project/issues/55143 is fixed
	// NOLINTNEXTLINE(bugprone-exception-escape)
	waitable(waitable&& w) noexcept(false);

	// TODO: remove lint suppression when https://github.com/llvm/llvm-project/issues/55143 is fixed
	// NOLINTNEXTLINE(bugprone-exception-escape)
	waitable& operator=(waitable&& w) noexcept(false);

	bool is_added() const noexcept
	{
		return this->is_added_to_waitset;
	}

	// Destructr is protected because this class is supposed to be used as a base class,
	// but is not supposed to be destroyed via base pointer.
	// TODO: is it possible to check it with static_assert? if so, add test and move this note there
	~waitable() noexcept
	{
		ASSERT(!this->is_added(), [](auto& o) {
			o << "~waitable(): the waitable is currently added to some wait_set()";
		})
	}

public:
	const decltype(readiness_flags)& flags() const noexcept
	{
		return this->readiness_flags;
	}

#if M_OS == M_OS_WINDOWS

protected:
	virtual HANDLE get_handle() = 0;

	virtual void set_waiting_flags(utki::flags<ready>) {}

	virtual bool check_signaled()
	{
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

} // namespace opros
