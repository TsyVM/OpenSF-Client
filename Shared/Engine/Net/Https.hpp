// HTTPS, for Team Vanilla's account service (TVAS: Docs/UniversalServerDeploy.md §4.1, §5.3). The
// game and the server reach TVAS only by its address and these requests, so it can move without a
// new build (TV-11).
//
// Certificates are always checked, by the system: WinHTTP's on Windows; on Linux and macOS the
// system's own libcurl, opened at run time, with the system's certificate store; on Android the
// phone's own (HttpsURLConnection, through the activity). Plain http is taken only for a
// loopback address (127.0.0.1, ::1, localhost): a test's own TVAS on the same machine, where there
// is no wire to read. Anything else must be https, and a certificate that does not check out fails
// the request.
#pragma once

#include "Engine/Core/Types.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace eng::net {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    double timeout = 15;   // seconds, the whole request
};

struct HttpResponse {
    int status = 0;        // 0: no answer (see `error`)
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;   // names lower case
    std::string error;     // why there was no answer: no connection, a bad certificate, a timeout
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    std::string header(std::string_view name) const;
};

// Whether a URL may be asked at all (https, or http to loopback); `why` says why not.
bool url_allowed(std::string_view url, std::string* why = nullptr);
// One request, waited for. Safe from any thread.
HttpResponse http_request(const HttpRequest& request);
// "LegacySF/<build> (<platform>)": every request names the game.
void set_user_agent(std::string agent);
#ifdef __ANDROID__
// The Java side that makes the requests (GameActivity.webRequest): named once, from the game's
// thread (a JavaVM* and the activity's jobject), before the first request.
void android_web_init(void* java_vm, void* activity);
#endif

// Requests made on worker threads, their answers handed back on the owner's thread by poll(): the
// server's tick and the game's frame never wait on the network.
class HttpWorker {
public:
    using Callback = std::function<void(HttpResponse)>;
    explicit HttpWorker(int threads = 2);
    ~HttpWorker();
    HttpWorker(const HttpWorker&) = delete;
    HttpWorker& operator=(const HttpWorker&) = delete;

    void submit(HttpRequest request, Callback done);
    // Runs the callbacks of requests that have finished, in the order they finished.
    void poll();
    // Requests not yet answered (queued or in flight).
    size_t pending() const;
    // Waits for everything queued to finish (or `seconds`), then runs their callbacks.
    void drain(double seconds);

private:
    struct Job {
        HttpRequest request;
        Callback done;
        HttpResponse response;
    };
    void run();
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::deque<Job> finished_;
    size_t in_flight_ = 0;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace eng::net
