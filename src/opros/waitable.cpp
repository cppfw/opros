#include "waitable.hpp"

#include <utki/exception.hpp>

using namespace opros;



waitable::waitable(waitable&& w) :
		is_added_to_waitset(false),
		user_data(w.user_data)
{
	// cannot move from waitable which is added to WaitSet
	if(w.is_added_to_waitset){
		throw std::invalid_argument("waitable::waitable(move): cannot move waitable which is added to WaitSet");
	}

	this->readiness_flags = std::move(w.readiness_flags);
	w.readiness_flags.clear();
	
	w.user_data = nullptr;
}



waitable& waitable::operator=(waitable&& w){
	if(this->is_added()){
		throw utki::invalid_state("waitable::waitable(move): cannot move while this waitable is added to WaitSet");
	}

	if(w.is_added()){
		throw std::invalid_argument("waitable::waitable(move): cannot move waitable which is added to WaitSet");
	}

	ASSERT(!this->is_added())

	this->readiness_flags = std::move(w.readiness_flags);
	w.readiness_flags.clear();

	this->user_data = w.user_data;
	w.user_data = nullptr;
	return *this;
}
