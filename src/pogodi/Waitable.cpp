#include "Waitable.hpp"
#include "WaitSet.hpp"


using namespace pogodi;



Waitable::Waitable(Waitable&& w) :
		isAdded_var(false),
		userData(w.userData),
		readinessFlags(NOT_READY)//Treat copied Waitable as NOT_READY
{
	//cannot move from waitable which is added to WaitSet
	if(w.isAdded_var){
		throw WaitSet::Exc("Waitable::Waitable(move): cannot move Waitable which is added to WaitSet");
	}

	const_cast<Waitable&>(w).clearAllReadinessFlags();
	const_cast<Waitable&>(w).userData = 0;
}



Waitable& Waitable::operator=(Waitable&& w){
	if(this->isAdded_var){
		throw WaitSet::Exc("Waitable::Waitable(move): cannot move while this Waitable is added to WaitSet");
	}

	if(w.isAdded_var){
		throw WaitSet::Exc("Waitable::Waitable(move): cannot move Waitable which is added to WaitSet");
	}

	ASSERT(!this->isAdded_var)

	//Clear readiness flags on moving.
	//Will need to wait for readiness again, using the WaitSet.
	this->clearAllReadinessFlags();
	const_cast<Waitable&>(w).clearAllReadinessFlags();

	this->userData = w.userData;
	const_cast<Waitable&>(w).userData = 0;
	return *this;
}
