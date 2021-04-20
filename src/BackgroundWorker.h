#pragma once

#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <deque>

class BackgroundWorker {
private:
  std::mutex m_mutex;
  std::thread m_thread;
  std::condition_variable m_signal;
  std::deque<std::function<void()>> m_queue;
  bool m_stop{ };

  void thread_func() {
    for (;;) {
      auto lock = std::unique_lock(m_mutex);
      m_signal.wait(lock, [&]() { return m_stop || !m_queue.empty(); });
      if (m_queue.empty())
        break;
      auto task = std::move(m_queue.front());
      m_queue.pop_front();
      lock.unlock();

      task();
    }
  }

public:
  BackgroundWorker()
    : m_thread(&BackgroundWorker::thread_func, this) {
  }

  ~BackgroundWorker() {
    auto lock = std::unique_lock(m_mutex);
    m_stop = true;
    lock.unlock();
    m_thread.join();
  }

  template<typename F>
  void execute(F&& function) {
    auto lock = std::unique_lock(m_mutex);
    m_queue.emplace_back(std::forward<F>(function));
    lock.unlock();
    m_signal.notify_one();
  }
};

