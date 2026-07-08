#include "decoderprocess.h"

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

    // Put the child into its own process group from the parent side too.
    // The child also calls setpgid(0, 0), but doing it here avoids a race
    // where stop() could run before the child reached setpgid().
    setpgid(pid_, pid_);

    close(pipefd[1]);
    stdout_fd_ = pipefd[0];
    fcntl(stdout_fd_, F_SETFL, fcntl(stdout_fd_, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

void DecoderProcess::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ > 0) {
        // Terminate the whole shell pipeline, not just /bin/sh.
        // pcmrecord and rs41mod inherit the same process group.
        kill(-pid_, SIGTERM);

        bool exited = false;
        for (int i = 0; i < 30; ++i) {
            int status = 0;
            pid_t r = waitpid(pid_, &status, WNOHANG);
            if (r == pid_) {
                exited = true;
                break;
            }
            usleep(100000);
        }

        if (!exited) {
            kill(-pid_, SIGKILL);
            for (int i = 0; i < 20; ++i) {
                int status = 0;
                pid_t r = waitpid(pid_, &status, WNOHANG);
                if (r == pid_) break;
                usleep(50000);
            }
        }

        // Reap any remaining state without blocking.
        waitpid(pid_, nullptr, WNOHANG);
        pid_ = -1;
    }

    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
    buffer_.clear();
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

std::optional<std::string> DecoderProcess::readLine() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stdout_fd_ < 0) return std::nullopt;

    struct pollfd pfd;
    pfd.fd = stdout_fd_;
    pfd.events = POLLIN | POLLHUP | POLLERR;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, 0);
    if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
        char tmp[1024];
        while (true) {
            ssize_t n = read(stdout_fd_, tmp, sizeof(tmp));
            if (n > 0) {
                buffer_.append(tmp, static_cast<size_t>(n));
                if (n < static_cast<ssize_t>(sizeof(tmp))) break;
            } else {
                break;
            }
        }
    }

    auto pos = buffer_.find('\n');
    if (pos == std::string::npos) {
        if (!isRunningUnlocked() && !buffer_.empty()) {
            std::string line = buffer_;
            buffer_.clear();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        return std::nullopt;
    }

    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}
