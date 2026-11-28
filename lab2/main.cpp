#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}
    ~Socket() { reset(); }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int get() const { return fd_; }

    void reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_;
};

volatile sig_atomic_t got_sighup = 0;
volatile sig_atomic_t stop_requested = 0;

void handle_signal(int signal_number) {
    if (signal_number == SIGHUP) {
        got_sighup = 1;
    } else {
        stop_requested = 1;
    }
}

bool configure_signals(sigset_t& wait_mask) {
    const auto signals = {SIGHUP, SIGINT, SIGTERM};
    sigset_t blocked;
    sigemptyset(&blocked);
    for (int signal_number : signals) {
        sigaddset(&blocked, signal_number);
    }

    if (sigprocmask(SIG_BLOCK, &blocked, &wait_mask) == -1) {
        std::perror("sigprocmask");
        return false;
    }

    struct sigaction action {};
    action.sa_handler = handle_signal;
    action.sa_mask = blocked;
    for (int signal_number : signals) {
        if (sigaction(signal_number, &action, nullptr) == -1) {
            std::perror("sigaction");
            return false;
        }
        sigdelset(&wait_mask, signal_number);
    }
    return true;
}

bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::perror("fcntl");
        return false;
    }
    return true;
}

bool start_listening(int fd, std::uint16_t port) {
    const int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        std::perror("setsockopt(SO_REUSEADDR)");
        return false;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        std::perror("bind");
        return false;
    }
    if (listen(fd, SOMAXCONN) == -1) {
        std::perror("listen");
        return false;
    }
    return true;
}

bool can_retry(int error) {
    return error == EINTR || error == EAGAIN || error == EWOULDBLOCK;
}

bool accept_client(int listener, Socket& client) {
    const int fd = accept(listener, nullptr, nullptr);
    if (fd == -1) {
        if (can_retry(errno) || errno == ECONNABORTED) {
            return true;
        }
        std::perror("accept");
        return false;
    }

    if (client.get() >= 0) {
        close(fd);
        std::printf("Новое подключение (fd %d) закрыто, сервер занят\n", fd);
    } else if (fd >= FD_SETSIZE || !set_nonblocking(fd)) {
        close(fd);
        std::printf("Новое подключение (fd %d) закрыто, сокет недоступен для pselect\n", fd);
    } else {
        client.reset(fd);
        std::printf("Новое подключение (fd %d) принято\n", fd);
    }
    return true;
}

void read_client(Socket& client) {
    const int fd = client.get();
    char buffer[4096];
    const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
    if (received > 0) {
        std::printf("Получено %zd байт (fd %d)\n", received, fd);
        return;
    }

    if (received < 0) {
        if (can_retry(errno)) {
            return;
        }
        std::perror("recv");
    }

    client.reset();
    std::printf("Клиент отключён (fd %d)\n", fd);
}

int run_server(std::uint16_t port, const sigset_t& wait_mask) {
    const Socket listener(socket(AF_INET, SOCK_STREAM, 0));
    const int listener_fd = listener.get();
    if (listener_fd == -1) {
        std::perror("socket");
        return EXIT_FAILURE;
    }
    if (listener_fd >= FD_SETSIZE) {
        std::fprintf(stderr, "Дескриптор сервера %d превышает предел pselect\n", listener_fd);
        return EXIT_FAILURE;
    }
    if (!set_nonblocking(listener_fd) || !start_listening(listener_fd, port)) {
        return EXIT_FAILURE;
    }

    Socket client;
    std::printf("Сервер слушает 0.0.0.0:%u (PID %ld)\n",
                static_cast<unsigned>(port), static_cast<long>(getpid()));

    for (;;) {
        const int client_fd = client.get();
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener_fd, &readable);
        if (client_fd >= 0) {
            FD_SET(client_fd, &readable);
        }

        const int ready = pselect(std::max(listener_fd, client_fd) + 1, &readable,
                                  nullptr, nullptr, nullptr, &wait_mask);
        if (ready == -1 && errno != EINTR) {
            std::perror("pselect");
            return EXIT_FAILURE;
        }
        if (got_sighup) {
            got_sighup = 0;
            std::puts("Получен SIGHUP");
        }
        if (stop_requested) {
            return EXIT_SUCCESS;
        }
        if (ready <= 0) {
            continue;
        }

        if (FD_ISSET(listener_fd, &readable) && !accept_client(listener_fd, client)) {
            return EXIT_FAILURE;
        }
        if (client_fd >= 0 && FD_ISSET(client_fd, &readable)) {
            read_client(client);
        }
    }
}

}

int main(int argc, char* argv[]) {
    unsigned port = 8080;
    if (argc == 2) {
        const std::string_view argument(argv[1]);
        const char* end = argument.data() + argument.size();
        const auto parsed = std::from_chars(argument.data(), end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || port == 0 || port > 65535) {
            port = 0;
        }
    }
    if (argc > 2 || port == 0) {
        std::fprintf(stderr, "Использование: %s [порт от 1 до 65535]\n", argv[0]);
        return EXIT_FAILURE;
    }

    sigset_t wait_mask;
    if (!configure_signals(wait_mask)) {
        return EXIT_FAILURE;
    }

    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    return run_server(static_cast<std::uint16_t>(port), wait_mask);
}
