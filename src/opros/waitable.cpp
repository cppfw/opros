/*
The MIT License (MIT)

Copyright (c) 2015-2021 Ivan Gagis <igagis@gmail.com>

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

#include "waitable.hpp"

using namespace opros;

waitable::waitable(waitable&& w)noexcept(false) :
		is_added_to_waitset(false),
		user_data(w.user_data)
{
	// cannot move from waitable which is added to WaitSet
	if(w.is_added_to_waitset){
		throw std::invalid_argument("waitable::waitable(move): cannot move waitable which is added to wait_set");
	}

	this->readiness_flags = std::move(w.readiness_flags);
	w.readiness_flags.clear();
	
	w.user_data = nullptr;
}

waitable& waitable::operator=(waitable&& w)noexcept(false){
	if(this->is_added()){
		throw std::logic_error("waitable::waitable(move): cannot move while this waitable is added to wait_set");
	}

	if(w.is_added()){
		throw std::invalid_argument("waitable::waitable(move): cannot move waitable which is added to wait_set");
	}

	ASSERT(!this->is_added())

	this->readiness_flags = std::move(w.readiness_flags);
	w.readiness_flags.clear();

	this->user_data = w.user_data;
	w.user_data = nullptr;
	return *this;
}
