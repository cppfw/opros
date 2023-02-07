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

void run(){
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

	queue.push_message([](){});

	thread.join();
}
}



namespace test_general{
void run(){
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
	q1.push_message([](){});
	{
		auto num_triggered = ws.wait();
		utki::assert(num_triggered == 1, [&](auto&o){o << "num_triggered = " << num_triggered;}, SL);
		utki::assert(ws.wait(utki::make_span(buf)) == 1, SL);
		utki::assert(buf[0].w == &q1, SL);
	}

	utki::assert(ws.wait(100) == 1, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 1, SL);
	utki::assert(buf[0].w == &q1, SL);

	// check that no objects trigger after reading from queue
	q1.peek_msg(); // should not block since one message was pushed before
	utki::assert(ws.wait(100) == 0, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 0, SL);



	// test Wait with 2 triggered objects
	q1.push_message([](){});
	q2.push_message([](){});
	utki::assert(ws.wait() == 2, SL);
	utki::assert(ws.wait(utki::make_span(buf)) == 2, SL);
	utki::assert((buf[0].w == &q1 && buf[1].w == &q2) || (buf[0].w == &q2 && buf[1].w == &q1), SL);

	utki::assert(ws.wait(100) == 2, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 2, SL);
	utki::assert((buf[0].w == &q1 && buf[1].w == &q2) || (buf[0].w == &q2 && buf[1].w == &q1), SL);

	// check that no objects trigger after reading from queue
	q1.peek_msg(); // should not block since one message was pushed before
	utki::assert(ws.wait(100) == 1, SL);
	utki::assert(ws.wait(100, utki::make_span(buf)) == 1, SL);
	utki::assert(buf[0].w == &q2, SL);

	q2.peek_msg(); // should not block since one message was pushed before
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
			q1.push_message([](){});
		});

		// TRACE(<< "waiting" << std::endl)
		utki::assert(ws.wait(std::numeric_limits<uint32_t>::max(), utki::make_span(buf)) == 1, SL);
		utki::assert(q1.peek_msg(), SL);
		utki::assert(!q1.peek_msg(), SL);

		thr.join();

		ws.remove(q1);
		ws.remove(q2);
	}
}
}

namespace test_threads{
class test_thread1{
public:
	std::thread thr;

	void start(){
		this->thr = std::thread([this](){this->run();});
	}

	void join(){
		this->thr.join();
	}

	helpers::queue queue;
	volatile bool quit_flag = false;

	void run(){
		opros::wait_set ws(1);
		
		ws.add(this->queue, {opros::ready::read});
		
		while(!this->quit_flag){
			ws.wait();
			while(auto m = this->queue.peek_msg()){
				m();
			}
		}
		
		ws.remove(this->queue);
	}
};

void run(){
	std::cout << "running threads test" << std::endl;

	//TODO: read ulimit
	size_t num_threads =
#if M_OS == M_OS_MACOSX
			50
#else
			500
#endif
	;

	std::vector<std::unique_ptr<test_thread1>> thr;

	for(size_t i = 0; i != num_threads; ++i){
		auto t = std::make_unique<test_thread1>();

		try{
			t->start();
		}catch(std::system_error& e){
			utki::log([&](auto& o){
				o << "exception caught during thread creation: " << e.what() << ",\n";
				o << "continuing to stopping already created threads" << '\n';
			});
			break;
		}

		thr.push_back(std::move(t));
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(1000));

	for(auto& t : thr){
		t->quit_flag = true;
		t->queue.push_message([](){});
		t->join();
	}
}

}