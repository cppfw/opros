#pragma once

#include <utki/debug.hpp>

#include "tests.hpp"

inline void test_wait_set(){
	test_general::run();
	test_message_queue_as_waitable::run();

	utki::log([&](auto&o){o << "[PASSED]: WaitSet test" << std::endl;});
}
