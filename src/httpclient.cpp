#include "httpclient.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <sys/wait.h>

#ifdef WSRX_HAVE_LIBCURL
#include <curl/curl.h>
#endif

namespace {

#ifdef WSRX_HAVE_LIBCURL
size_t discardBody(char*, size_t size, size_t nmemb, void*) {
    return size * nmemb;
}

int abortCheck(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* flag = static_cast<const std::atomic<bool>*>(clientp);
    return (flag != nullptr && flag->load()) ? 1 : 0;  
}

const std::string& userAgent() {
    static const std::string ua = [] {
        const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
        return std::string("curl/") + ((v != nullptr && v->version != nullptr) ? v->version : "");
    }();
    return ua;
}
#else
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

bool runCurl(const std::string& cmd, std::string& error) {
    const int rc = std::system(cmd.c_str());
    if (rc == 0) return true;
    if (rc != -1 && WIFEXITED(rc)) {
        error = "curl exit code " + std::to_string(WEXITSTATUS(rc));
    } else {
        error = "curl could not be run";
    }
    return false;
}
#endif

}  // namespace

void HttpClient::globalInit() {
#ifdef WSRX_HAVE_LIBCURL
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
#endif
}

const char* HttpClient::backendName() {
#ifdef WSRX_HAVE_LIBCURL
    return "libcurl (persistent connections)";
#else
    return "curl program (install libcurl4-openssl-dev and rebuild for persistent connections)";
#endif
}

HttpClient::HttpClient() {
#ifdef WSRX_HAVE_LIBCURL
    globalInit();
    curl_ = curl_easy_init();
    headers_ = curl_slist_append(nullptr, "Content-Type: application/json");
#endif
}

HttpClient::~HttpClient() {
#ifdef WSRX_HAVE_LIBCURL
    if (curl_ != nullptr) curl_easy_cleanup(static_cast<CURL*>(curl_));
    if (headers_ != nullptr) curl_slist_free_all(static_cast<curl_slist*>(headers_));
#endif
}

bool HttpClient::postJson(const std::string& url, const std::string& body, int timeout_sec, std::string& error) {
#ifdef WSRX_HAVE_LIBCURL
    return perform(url, &body, timeout_sec, error);
#else
    const std::string cmd = "curl -fsS -m " + std::to_string(timeout_sec) +
                            " -X POST -H 'Content-Type: application/json' --data " + shellQuote(body) + " " +
                            shellQuote(url) + " >/dev/null";
    return runCurl(cmd, error);
#endif
}

bool HttpClient::get(const std::string& url, int timeout_sec, std::string& error) {
#ifdef WSRX_HAVE_LIBCURL
    return perform(url, nullptr, timeout_sec, error);
#else
    const std::string cmd = "curl -fsS -m " + std::to_string(timeout_sec) + " -X GET " + shellQuote(url) + " >/dev/null";
    return runCurl(cmd, error);
#endif
}

bool HttpClient::perform(const std::string& url, const std::string* post_body, int timeout_sec, std::string& error) {
#ifdef WSRX_HAVE_LIBCURL
    CURL* curl = static_cast<CURL*>(curl_);
    if (curl == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    curl_easy_reset(curl);

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = '\0';
    const long timeout = std::max(1, timeout_sec);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(timeout, 5L));
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardBody);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    if (abort_flag_ != nullptr) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortCheck);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(abort_flag_));
    }

    if (post_body != nullptr) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body->size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, static_cast<curl_slist*>(headers_));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) return true;

    if (rc == CURLE_HTTP_RETURNED_ERROR) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        error = "HTTP " + std::to_string(status);
    } else {
        error = errbuf[0] != '\0' ? std::string(errbuf) : std::string(curl_easy_strerror(rc));
    }
    return false;
#else
    (void)url;
    (void)post_body;
    (void)timeout_sec;
    error = "libcurl support not compiled in";
    return false;
#endif
}
