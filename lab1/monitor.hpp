#pragma once

#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

struct Event {
    explicit Event(std::uint64_t number) : number(number) {}

    std::uint64_t number;
};

class Monitor {
public:
    void provide(std::unique_ptr<Event> event) {
        if (!event) {
            throw std::invalid_argument("Требуется объект события");
        }

        std::unique_lock lock(mutex_);
        state_changed_.wait(lock, [this] { return pending_event_ == nullptr; });

        pending_event_ = std::move(event);
        std::cout << "Поставщик отправил событие №" << pending_event_->number << std::endl;

        lock.unlock();
        state_changed_.notify_one();
    }

    void consume() {
        std::unique_lock lock(mutex_);
        state_changed_.wait(lock, [this] { return pending_event_ != nullptr; });

        auto event = std::move(pending_event_);
        // Вывод под мьютексом сохраняет порядок сообщений об отправке и обработке.
        std::cout << "Потребитель обработал событие №" << event->number << std::endl;

        lock.unlock();
        state_changed_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable state_changed_;
    std::unique_ptr<Event> pending_event_;
};
