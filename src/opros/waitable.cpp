#include "waitable.hpp"

using namespace opros;

waitable::waitable(waitable&& w) :
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

waitable& waitable::operator=(waitable&& w){
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
