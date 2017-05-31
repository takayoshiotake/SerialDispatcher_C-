//
//  serial_dispatcher.h
//
//  Copyright © 2016 OTAKE Takayoshi. All rights reserved.
//

#ifndef serial_dispatcher_h
#define serial_dispatcher_h

#include <mutex>
#include <future>
#include <thread>
#include <functional>
#include <deque>

struct serial_dispatcher {
private:
    std::recursive_mutex api_mutex_;
    bool is_running_ = false;
    
    std::thread thread_;
    bool requires_stop_ = false;
    
    std::mutex works_mutex_;
    std::deque<std::function<void(void)>> works_;
    std::condition_variable workable_;
    
public:
    serial_dispatcher() {
    }
    
    ~serial_dispatcher() {
        stop();
    }
    
    bool is_running() const {
        return is_running_;
    }
    
    void start() {
        std::lock_guard<std::recursive_mutex> api_lock(api_mutex_);
        if (is_running_) {
            return;
        }
        is_running_ = true;
        requires_stop_ = false;
        // Clear all works
        {
            std::lock_guard<std::mutex> lock(works_mutex_);
            works_.clear();
        }
        thread_ = std::thread(&serial_dispatcher::execute, this);
    }
    
    void stop() {
        std::lock_guard<std::recursive_mutex> api_lock(api_mutex_);
        if (!is_running_) {
            return;
        }
        if (thread_.joinable()) {
            async([&]() {
                requires_stop_ = true;
            });
            thread_.join();
        }
        is_running_ = false;
    }
    
    void async(std::function<void(void)>&& work) {
        if (std::this_thread::get_id() != thread_.get_id()) {
            std::lock_guard<std::recursive_mutex> api_lock(api_mutex_);
            if (!is_running_) {
                return;
            }
            std::lock_guard<std::mutex> lock(works_mutex_);
            works_.push_back(work);
            workable_.notify_all();
        }
        else {
            if (!is_running_) {
                return;
            }
            std::lock_guard<std::mutex> lock(works_mutex_);
            works_.push_back(work);
            workable_.notify_all();
        }
    }
    
    void sync(std::function<void(void)>&& work) {
        if (std::this_thread::get_id() != thread_.get_id()) {
            std::lock_guard<std::recursive_mutex> api_lock(api_mutex_);
            if (!is_running_) {
                return;
            }
            std::promise<void> promise;
            {
                std::lock_guard<std::mutex> lock(works_mutex_);
                works_.push_back(work);
                works_.push_back([&promise]() {
                    promise.set_value();
                });
                workable_.notify_all();
            }
            // Make sync
            promise.get_future().get();
        }
        else {
            if (!is_running_) {
                return;
            }
            work();
        }
    }
private:
    void execute() {
        while (!requires_stop_) {
            std::function<void(void)> function;
            {
                std::unique_lock<std::mutex> lock(works_mutex_);
                if (works_.empty()) {
                    workable_.wait(lock);
                }
                function = works_.front();
                works_.pop_front();
            }
            function();
        }
    }

    serial_dispatcher(const serial_dispatcher&) = delete;
    serial_dispatcher& operator =(const serial_dispatcher&) = delete;
};

#endif /* serial_dispatcher_h */
