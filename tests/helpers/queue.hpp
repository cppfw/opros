#pragma once

#include <utki/config.hpp>
#include <utki/debug.hpp>
#include <utki/spin_lock.hpp>

#include "../../src/opros/waitable.hpp"

#include <list>
#include <functional>

namespace helpers{

/**
 * @brief Message queue.
 * Message queue is used for communication of separate threads by
 * means of sending messages to each other. Thus, when one thread sends a message to another one,
 * it asks that another thread to execute some code portion - handler code of the message.
 * NOTE: queue implements waitable interface which means that it can be used in conjunction
 * with opros::WaitSet. But, note, that the implementation of the waitable is that it
 * shall only be used to wait for READ. If you are trying to wait for WRITE the behavior will be
 * undefined.
 */
class queue :
	public opros::waitable
{
public:
	using message_type = std::function<void()>;
	
private:
	utki::spin_lock mut;

	std::list<message_type> messages;
	
#if CFG_OS == CFG_OS_WINDOWS
#elif CFG_OS == CFG_OS_MACOSX
	// use pipe to implement waitable in *nix systems
	// one end will be saved in waitable::handle
	// and the other one in this member variable
	int pipe_end{};
#elif CFG_OS == CFG_OS_LINUX
#else
#	error "Unsupported OS"
#endif

public:

	queue(const queue&) = delete;
	queue& operator=(const queue&) = delete;

	queue(queue&&) = delete;
	queue& operator=(queue&&) = delete;

	/**
	 * @brief Constructor, creates empty message queue.
	 */
	queue();
	
	/**
	 * @brief Destructor.
	 * When called, it also destroys all messages on the queue.
	 */
	~queue()noexcept
#if CFG_OS == CFG_OS_WINDOWS
	override
#endif
	;

	/**
	 * @brief Pushes a new message to the queue.
	 * @param msg - the message to push into the queue.
	 */
	void push_message(message_type&& msg);

	/**
	 * @brief Get message from queue, does not block if no messages queued.
	 * This method gets a message from message queue. If there are no messages on the queue
	 * it will return invalid auto pointer.
	 * @return auto-pointer to Message instance.
	 * @return invalid auto-pointer if there are no messages in the queue.
	 */
	message_type peek_msg();

#if CFG_OS == CFG_OS_WINDOWS
protected:
	void set_waiting_flags(utki::flags<opros::ready>)override;
	utki::flags<opros::ready> get_readiness_flags()override;

#elif CFG_OS == CFG_OS_LINUX
#elif CFG_OS == CFG_OS_MACOSX
#else
#	error "Unsupported OS"
#endif
};

}
