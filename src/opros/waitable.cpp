#include "waitable.hpp"
#include "wait_set.hpp"

#include <utki/exception.hpp>

using namespace opros;



waitable::waitable(waitable&& w) :
		isAdded_var(false),
		userData(w.userData),
		readinessFlags(NOT_READY)//Treat copied waitable as NOT_READY
{
	// cannot move from waitable which is added to WaitSet
	if(w.isAdded_var){
		throw std::invalid_argument("waitable::waitable(move): cannot move waitable which is added to WaitSet");
	}

	const_cast<waitable&>(w).clearAllReadinessFlags();
	const_cast<waitable&>(w).userData = 0;
}



waitable& waitable::operator=(waitable&& w){
	if(this->isAdded_var){
		throw utki::invalid_state("waitable::waitable(move): cannot move while this waitable is added to WaitSet");
	}

	if(w.isAdded_var){
		throw std::invalid_argument("waitable::waitable(move): cannot move waitable which is added to WaitSet");
	}

	ASSERT(!this->isAdded_var)

	//Clear readiness flags on moving.
	//Will need to wait for readiness again, using the WaitSet.
	this->clearAllReadinessFlags();
	const_cast<waitable&>(w).clearAllReadinessFlags();

	this->userData = w.userData;
	const_cast<waitable&>(w).userData = 0;
	return *this;
}
