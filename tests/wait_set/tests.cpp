#include <vector>
#include <thread>

#include <utki/debug.hpp>
#include "../../src/opros/wait_set.hpp"
#include "../helpers/queue.hpp"

#include "tests.hpp"

#ifdef assert
#	undef assert
#endif

namespace test_message_queue_as_waitable{

void Run(){
	opros::wait_set ws(1);

	helpers::queue queue;

	std::thread thread([&queue](){
		opros::wait_set ws(1);
		ws.add(queue, utki::flags<opros::ready>().set(opros::ready::read));
		auto res = ws.wait(3000);
		utki::assert(res == 1, SL);
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

	std::array<opros::event_info, 4> buf;



	// test wait() with zero timeout, no objects should trigger, so, expecting return value of 0.
	utki::assert(ws.wait(0) == 0, SL);
	utki::assert(ws.wait(0, utki::make_span(buf)) == 0, SL);



	// test wait() with non-zero timeout, no objects should trigger, so, expecting return value of 0.
	utki::assert(ws.wait(100) == 0, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 0, SL);



	// test Wait with 1 triggered object
	q1.pushMessage([](){});
	utki::assert(ws.wait() == 1, SL);
	utki::assert(ws.wait(utki::make_span(buf)) == 1, SL);
	utki::assert(buf[0].w == &q1, SL);

	utki::assert(ws.wait(100) == 1, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 1, SL);
	utki::assert(buf[0].w == &q1, SL);

	// check that no objects trigger after reading from queue
	q1.peekMsg(); // should not block since one message was pushed before
	utki::assert(ws.wait(100) == 0, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 0, SL);



	// test Wait with 2 triggered objects
	q1.pushMessage([](){});
	q2.pushMessage([](){});
	utki::assert(ws.wait() == 2, SL);
	utki::assert(ws.wait(utki::make_span(buf)) == 2, SL);
	utki::assert((buf[0].w == &q1 && buf[1].w == &q2) || (buf[0].w == &q2 && buf[1].w == &q1), SL);

	utki::assert(ws.wait(100) == 2, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 2, SL);
	utki::assert((buf[0].w == &q1 && buf[1].w == &q2) || (buf[0].w == &q2 && buf[1].w == &q1), SL);

	// check that no objects trigger after reading from queue
	q1.peekMsg(); // should not block since one message was pushed before
	utki::assert(ws.wait(100) == 1, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 1, SL);
	utki::assert(buf[0].w == &q2, SL);

	q2.peekMsg(); // should not block since one message was pushed before
	utki::assert(ws.wait(100) == 0, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 0, SL);

	ws.remove(q1);
	ws.remove(q2);

	// test waiting with timeout equal to max value of uint32_t
	{
		opros::wait_set ws(4);

		helpers::queue q1, q2;

		ws.add(q1, utki::make_flags({opros::ready::read}));
		ws.add(q2, utki::make_flags({opros::ready::read}));

		utki::assert(ws.size() == 2, SL);

		std::array<opros::event_info, 4> buf;

		std::thread thr([&q1](){
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			// TRACE(<< "pushing message" << std::endl)
			q1.pushMessage([](){});
		});

		// TRACE(<< "waiting" << std::endl)
		utki::assert(ws.wait(std::numeric_limits<uint32_t>::max(), utki::make_span(buf)) == 1, SL);
		utki::assert(q1.peekMsg(), SL);
		utki::assert(!q1.peekMsg(), SL);

		thr.join();

		ws.remove(q1);
		ws.remove(q2);
	}
}
}
