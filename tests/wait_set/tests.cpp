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
		ws.add(queue, utki::flags<opros::ready>().set(opros::ready::read), nullptr);
		auto res = ws.wait(3000);
		utki::assert(res, SL);
		utki::assert(ws.get_triggered().size() == 1, SL);
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

	ws.add(q1, utki::make_flags({opros::ready::read}), &q1);
	ws.add(q2, utki::make_flags({opros::ready::read}), &q2);

	// test wait() with zero timeout, no objects should trigger, so, expecting return value of 0.
	utki::assert(!ws.wait(0), SL);

	// test wait() with non-zero timeout, no objects should trigger, so, expecting return value of 0.
	utki::assert(!ws.wait(100), SL);

	// test Wait with 1 triggered object
	q1.push_message([](){});
	{
		ws.wait();
		utki::assert(ws.get_triggered().size() == 1, [&](auto&o){o << "num_triggered = " << ws.get_triggered().size();}, SL);
		utki::assert(ws.get_triggered()[0].user_data == &q1, SL);
	}

	utki::assert(ws.wait(100), SL);
	utki::assert(ws.get_triggered().size() == 1, SL);
	utki::assert(ws.get_triggered()[0].user_data == &q1, SL);

	// check that no objects trigger after reading from queue
	q1.peek_msg(); // should not block since one message was pushed before
	utki::assert(!ws.wait(100), SL);
	utki::assert(ws.get_triggered().empty(), SL);

	// test Wait with 2 triggered objects
	q1.push_message([](){});
	q2.push_message([](){});
	ws.wait();
	utki::assert(ws.get_triggered().size() == 2, SL);
	utki::assert(
		(ws.get_triggered()[0].user_data == &q1 && ws.get_triggered()[1].user_data == &q2)
			|| (ws.get_triggered()[0].user_data == &q2 && ws.get_triggered()[1].user_data == &q1),
		SL
	);

	utki::assert(ws.wait(100), SL);
	utki::assert(ws.get_triggered().size() == 2, SL);
	utki::assert(
		(ws.get_triggered()[0].user_data == &q1 && ws.get_triggered()[1].user_data == &q2)
			|| (ws.get_triggered()[0].user_data == &q2 && ws.get_triggered()[1].user_data == &q1),
		SL
	);

	// check that no objects trigger after reading from queue
	q1.peek_msg(); // should not block since one message was pushed before
	utki::assert(ws.wait(100), SL);
	utki::assert(ws.get_triggered().size() == 1, SL);
	utki::assert(ws.get_triggered()[0].user_data == &q2, SL);

	q2.peek_msg(); // should not block since one message was pushed before
	utki::assert(!ws.wait(100), SL);
	utki::assert(ws.get_triggered().empty(), SL);

	ws.remove(q1);
	ws.remove(q2);

	// test waiting with timeout equal to max value of uint32_t
	{
		opros::wait_set ws(4);

		helpers::queue q1, q2;

		ws.add(q1, utki::make_flags({opros::ready::read}), &q1);
		ws.add(q2, utki::make_flags({opros::ready::read}), &q2);

		utki::assert(ws.size() == 2, SL);

		std::thread thr([&q1](){
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			// TRACE(<< "pushing message" << std::endl)
			q1.push_message([](){});
		});

		// TRACE(<< "waiting" << std::endl)
		utki::assert(ws.wait(std::numeric_limits<uint32_t>::max()), SL);
		utki::assert(ws.get_triggered().size() == 1, SL);
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
		
		ws.add(this->queue, {opros::ready::read}, nullptr);
		
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
#if CFG_OS == CFG_OS_MACOSX
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