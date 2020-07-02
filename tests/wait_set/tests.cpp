#include <vector>
#include <thread>

#include <utki/debug.hpp>
#include "../../src/opros/wait_set.hpp"
#include "../helpers/queue.hpp"

#include "tests.hpp"



namespace test_message_queue_as_waitable{

void Run(){
	opros::wait_set ws(1);

	helpers::queue queue;

	std::thread thread([&queue](){
		opros::wait_set ws(1);
		ws.add(queue, utki::flags<opros::ready>().set(opros::ready::read));
		auto res = ws.wait(3000);
		ASSERT_ALWAYS(res == 1)
		ws.remove(queue);
	});

	std::this_thread::sleep_for(std::chrono::seconds(1));

	queue.pushMessage([](){});

	thread.join();
}
}



namespace test_general{
void Run(){
	opros::wait_set ws(4);

	helpers::queue q1, q2;

	ws.add(q1, utki::make_flags({opros::ready::read}));
	ws.add(q2, utki::make_flags({opros::ready::read}));

	std::array<opros::waitable*, 4> buf;



	// test wait() with zero timeout, no objects should trigger, so, expecting return value of 0.
	ASSERT_ALWAYS(ws.wait(0) == 0)
	ASSERT_ALWAYS(ws.wait(0, utki::make_span(buf)) == 0)



	// test wait() with non-zero timeout, no objects should trigger, so, expecting return value of 0.
	ASSERT_ALWAYS(ws.wait(100) == 0)
	ASSERT_ALWAYS(ws.wait(100, utki::make_span(buf)) == 0)



	// test Wait with 1 triggered object
	q1.pushMessage([](){});
	ASSERT_ALWAYS(ws.wait() == 1)
	ASSERT_ALWAYS(ws.wait(utki::make_span(buf)) == 1)
	ASSERT_ALWAYS(buf[0] == &q1)

	ASSERT_ALWAYS(ws.wait(100) == 1)
	ASSERT_ALWAYS(ws.wait(100, utki::make_span(buf)) == 1)
	ASSERT_ALWAYS(buf[0] == &q1)
	ASSERT_ALWAYS(!q2.flags().get(opros::ready::read))

	// check that no objects trigger after reading from queue
	q1.peekMsg(); // should not block since one message was pushed before
	ASSERT_ALWAYS(ws.wait(100) == 0)
	ASSERT_ALWAYS(ws.wait(100, utki::make_span(buf)) == 0)



	// test Wait with 2 triggered objects
	q1.pushMessage([](){});
	q2.pushMessage([](){});
	ASSERT_ALWAYS(ws.wait() == 2)
	ASSERT_ALWAYS(ws.wait(utki::make_span(buf)) == 2)
	ASSERT_ALWAYS((buf[0] == &q1 && buf[1] == &q2) || (buf[0] == &q2 && buf[1] == &q1))

	ASSERT_ALWAYS(ws.wait(100) == 2)
	ASSERT_ALWAYS(ws.wait(100, utki::make_span(buf)) == 2)
	ASSERT_ALWAYS((buf[0] == &q1 && buf[1] == &q2) || (buf[0] == &q2 && buf[1] == &q1))

	// check that no objects trigger after reading from queue
	q1.peekMsg(); // should not block since one message was pushed before
	ASSERT_ALWAYS(ws.wait(100) == 1)
	ASSERT_ALWAYS(ws.wait(100, utki::make_span(buf)) == 1)
	ASSERT_ALWAYS(buf[0] == &q2)

	q2.peekMsg(); // should not block since one message was pushed before
	ASSERT_ALWAYS(ws.wait(100) == 0)
	ASSERT_ALWAYS(ws.wait(100, utki::make_span(buf)) == 0)

	ws.remove(q1);
	ws.remove(q2);

	// test waiting with timeout equal to uint32_t(-1)
	{
		opros::wait_set ws(4);

		helpers::queue q1, q2;

		ws.add(q1, utki::make_flags({opros::ready::read}));
		ws.add(q2, utki::make_flags({opros::ready::read}));

		std::array<opros::waitable*, 4> buf;



		ASSERT_ALWAYS(ws.wait(uint32_t(-1), utki::make_span(buf)) == 0)
	}
}
}
