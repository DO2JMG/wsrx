#include "decoderprocess.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

DecoderProcess::~DecoderProcess() {
    stop();
}

bool DecoderProcess::start(const std::string& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isRunningUnlocked()) return false;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return false;
    }

    pid_ = fork();
    if (pid_ < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        pid_ = -1;
        return false;
    }

    if (pid_ == 0) {
        setpgid(0, 0);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    setpgid(pid_, pid_);

    close(pipefd[1]);
    stdout_fd_ = pipefd[0];
    fcntl(stdout_fd_, F_SETFL, fcntl(stdout_fd_, F_GETFL, 0) | O_NONBLOCK);
    eof_ = false;
    buffer_.clear();
    buffer_pos_ = 0;
    return true;
}

void DecoderProcess::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ > 0) {
        kill(-pid_, SIGTERM);

        auto reaped = [this]() {
            int status = 0;
            const pid_t r = waitpid(pid_, &status, WNOHANG);
            return r == pid_ || (r < 0 && errno == ECHILD);
        };

        bool exited = false;
        for (int i = 0; i < 30; ++i) {
            if (reaped()) {
                exited = true;
                break;
            }
            usleep(100000);
        }

        if (!exited) {
            kill(-pid_, SIGKILL);
            for (int i = 0; i < 20; ++i) {
                if (reaped()) break;
                usleep(50000);
            }
        }

        waitpid(pid_, nullptr, WNOHANG);
        pid_ = -1;
    }

    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
    eof_ = false;
    buffer_.clear();
    buffer_pos_ = 0;
}

bool DecoderProcess::isRunningUnlocked() const {
    if (pid_ <= 0) return false;
    int status = 0;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    return r == 0;
}

bool DecoderProcess::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isRunningUnlocked();
}

bool DecoderProcess::atEof() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return eof_;
}

std::optional<std::string> DecoderProcess::popLineUnlocked() {
    const size_t nl = buffer_.find('\n', buffer_pos_);
    if (nl == std::string::npos) return std::nullopt;

    std::string line = buffer_.substr(buffer_pos_, nl - buffer_pos_);
    buffer_pos_ = nl + 1;
    if (buffer_pos_ >= buffer_.size()) {
        buffer_.clear();
        buffer_pos_ = 0;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

std::optional<std::string> DecoderProcess::readLine(int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto line = popLineUnlocked()) return line;
    if (stdout_fd_ < 0) return std::nullopt;

    if (!eof_) {
        struct pollfd pfd;
        pfd.fd = stdout_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        const int pr = poll(&pfd, 1, timeout_ms < 0 ? 0 : timeout_ms);
        if (pr > 0) {
            if (buffer_pos_ > 0) {
                buffer_.erase(0, buffer_pos_);
                buffer_pos_ = 0;
            }
            char tmp[4096];
            while (true) {
                const ssize_t n = read(stdout_fd_, tmp, sizeof(tmp));
                if (n > 0) {
                    buffer_.append(tmp, static_cast<size_t>(n));
                    if (n < static_cast<ssize_t>(sizeof(tmp))) break;
                } else if (n == 0) {
                    eof_ = true;
                    break;
                } else {
                    if (errno == EINTR) continue;
                    break;  
                }
            }
        }
    }

    if (auto line = popLineUnlocked()) return line;

    // Hand out a last line without trailing newline once the decoder is gone.
    if (buffer_pos_ < buffer_.size() && (eof_ || !isRunningUnlocked())) {
        std::string line = buffer_.substr(buffer_pos_);
        buffer_.clear();
        buffer_pos_ = 0;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }
    return std::nullopt;
}

