#pragma once

#include <atomic>
#include <string>


class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    static void globalInit();
    static const char* backendName();

    bool postJson(const std::string& url, const std::string& body, int timeout_sec, std::string& error);
    bool get(const std::string& url, int timeout_sec, std::string& error);

    void setAbortFlag(const std::atomic<bool>* flag) { abort_flag_ = flag; }

private:
    bool perform(const std::string& url, const std::string* post_body, int timeout_sec, std::string& error);

    void* curl_ = nullptr;   
    void* headers_ = nullptr;  
    const std::atomic<bool>* abort_flag_ = nullptr;
};
