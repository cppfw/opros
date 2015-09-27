#pragma once

#include <utki/debug.hpp>

#include "tests.hpp"



inline void TestTingWaitSet(){
	test_general::Run();
	test_message_queue_as_waitable::Run();

	TRACE_ALWAYS(<< "[PASSED]: WaitSet test" << std::endl)
}
