#include "monitor.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

int main() {
    Monitor monitor;

    std::thread producer([&monitor] {
        using namespace std::chrono_literals;

        for (std::uint64_t number = 1;; ++number) {
            std::this_thread::sleep_for(1s);
            monitor.provide(std::make_unique<Event>(number));
        }
    });

    std::thread consumer([&monitor] {
        for (;;) {
            monitor.consume();
        }
    });

    producer.join();
    consumer.join();
}
