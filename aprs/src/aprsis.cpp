#include "aprsis.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

AprsIsClient::AprsIsClient(const AprsConfig& cfg, Logger& log) : cfg_(cfg), log_(log) {}

AprsIsClient::~AprsIsClient() { stop(); }

void AprsIsClient::start() {
    shutdown_ = false;
    thread_ = std::thread(&AprsIsClient::connectionLoop, this);
}

void AprsIsClient::stop() {
    if (shutdown_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(sock_mutex_);
        disconnectLocked();
    }
    if (thread_.joinable()) thread_.join();
}

void AprsIsClient::disconnectLocked() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    connected_ = false;
}

bool AprsIsClient::connectOnce() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(cfg_.server_port);
    int rc = ::getaddrinfo(cfg_.server.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        log_.warn("APRS-IS: could not resolve " + cfg_.server + ": " + gai_strerror(rc));
        return false;
    }

    int fd = -1;
    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        if (shutdown_.load()) break;

        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int cr = ::connect(fd, p->ai_addr, p->ai_addrlen);
        bool connected = (cr == 0);
        if (cr < 0 && errno == EINPROGRESS) {
            const int total_timeout_sec = 10;
            for (int waited = 0; waited < total_timeout_sec && !shutdown_.load(); ++waited) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(fd, &wfds);
                timeval tv{};
                tv.tv_sec = 1;
                int sel = ::select(fd + 1, nullptr, &wfds, nullptr, &tv);
                if (sel > 0) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    connected = (err == 0);
                    break;
                }

                if (sel < 0) break;
            }
        }

        ::fcntl(fd, F_SETFL, flags);  

        if (connected) break;
        ::close(fd);
        fd = -1;
        if (shutdown_.load()) break;
    }
    ::freeaddrinfo(result);

    if (fd >= 0) {
        timeval tv{};
        tv.tv_sec = 10;
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    if (fd < 0) {
        log_.warn("APRS-IS: connect to " + cfg_.server + ":" + port_str + " failed");
        return false;
    }

    std::string login = "user " + cfg_.callsign + " pass " + cfg_.passcode + " vers wsrx-aprs-gw 1.0";
    if (!cfg_.filter.empty()) login += " filter " + cfg_.filter;
    login += "\r\n";

    ssize_t sent = ::send(fd, login.data(), login.size(), 0);
    if (sent < 0 || static_cast<size_t>(sent) != login.size()) {
        log_.warn("APRS-IS: login send failed");
        ::close(fd);
        return false;
    }

    char buf[512];
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        std::string resp(buf);
        while (!resp.empty() && (resp.back() == '\n' || resp.back() == '\r')) resp.pop_back();
        log_.info("APRS-IS: connected to " + cfg_.server + ":" + port_str + " (" + resp + ")");
    } else {
        log_.info("APRS-IS: connected to " + cfg_.server + ":" + port_str);
    }

    {
        std::lock_guard<std::mutex> lock(sock_mutex_);
        sock_ = fd;
    }
    connected_ = true;
    return true;
}

void AprsIsClient::connectionLoop() {
    int backoff_sec = cfg_.reconnect_min_sec > 0 ? cfg_.reconnect_min_sec : 5;
    const int max_backoff = cfg_.reconnect_max_sec > 0 ? cfg_.reconnect_max_sec : 300;

    while (!shutdown_.load()) {
        if (!connected_.load()) {
            if (connectOnce()) {
                backoff_sec = cfg_.reconnect_min_sec > 0 ? cfg_.reconnect_min_sec : 5;  // reset on success
            } else {
                for (int waited = 0; waited < backoff_sec && !shutdown_.load(); ++waited) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                backoff_sec = std::min(backoff_sec * 2, max_backoff);
                continue;
            }
        }

        int fd;
        {
            std::lock_guard<std::mutex> lock(sock_mutex_);
            fd = sock_;
        }
        if (fd < 0) continue;

        char buf[256];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            log_.warn("APRS-IS: server closed the connection, reconnecting");
            std::lock_guard<std::mutex> lock(sock_mutex_);
            disconnectLocked();
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;  // just a recv timeout, connection still alive
            }
            log_.warn(std::string("APRS-IS: connection error (") + std::strerror(errno) + "), reconnecting");
            std::lock_guard<std::mutex> lock(sock_mutex_);
            disconnectLocked();
        }
        // n > 0: server chatter (comments/keepalive), ignored.
    }

    std::lock_guard<std::mutex> lock(sock_mutex_);
    disconnectLocked();
}

bool AprsIsClient::sendLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    if (sock_ < 0 || !connected_.load()) return false;

    std::string out = line + "\r\n";
    ssize_t sent = ::send(sock_, out.data(), out.size(), MSG_NOSIGNAL);
    if (sent < 0 || static_cast<size_t>(sent) != out.size()) {
        log_.warn("APRS-IS: send failed, will reconnect");
        disconnectLocked();
        return false;
    }
    return true;
}
