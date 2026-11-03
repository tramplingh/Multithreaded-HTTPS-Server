#include "thread_pool.hpp"

#include <utility>
using namespace std;

ThreadPool::ThreadPool(size_t num_threads, size_t max_queue_size)
 : stop(false), max_queue(max_queue_size){
       	 workers.reserve(num_threads);
	for(size_t i = 0; i < num_threads; ++i){
		workers.emplace_back([this] { worker_loop(); });
	}
}
	
ThreadPool::~ThreadPool(){
	{
		lock_guard<mutex> lock(queue_mutex);
		stop = true;
	}
	condition.notify_all();
	for(thread& w : workers){
		if(w.joinable()) w.join();
	}
}

bool ThreadPool::enqueue(function<void()> task){
	{
		lock_guard<mutex> lock(queue_mutex);
		if(stop || tasks.size() >= max_queue) return false;
		tasks.push(move(task));
	}
	condition.notify_one();
	return true;
}

void ThreadPool::worker_loop(){
	while(true){
		function<void()> task;
		{
			unique_lock<mutex> lock(queue_mutex);
			condition.wait(lock, [this] {
				return stop  || !tasks.empty();
			});
			if(stop && tasks.empty()) return;
			
			task = move(tasks.front());
			tasks.pop();
		}
		try{
			task();
		} catch(...){
		}
	}
}	
