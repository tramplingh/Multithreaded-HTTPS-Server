//Fixed set of workers that pull tasks from a shared queue.

// accept() thread --enqueue()--> [ task queue ] --> worker 1..N --> task()

#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
using namespace std;

class ThreadPool{
	public:
		ThreadPool(size_t num_threads, size_t max_queue = 1024);
		
		~ThreadPool();
		
		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator = (const ThreadPool&) = delete;
		
		bool enqueue(function<void()> task);
		
	private:
		void worker_loop();
		
		vector<thread> workers;
		queue<function<void()>> tasks;
		mutex queue_mutex;
		condition_variable condition;
		bool stop;
		size_t max_queue;
};
