#include "Engine/Net/Https.hpp"

#include "Engine/Core/Strings.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#elif defined(__ANDROID__)
#include <jni.h>
#else
#include <dlfcn.h>
#endif

namespace eng::net {

namespace {

std::mutex g_agent_mutex;
std::string g_agent = "LegacySF";

std::string agent() {
    std::lock_guard lock(g_agent_mutex);
    return g_agent;
}

struct UrlParts {
    bool secure = false;
    std::string host;
    u16 port = 0;
    std::string path;   // with the query
};

bool split_url(std::string_view url, UrlParts& out) {
    if (url.starts_with("https://")) out.secure = true, url.remove_prefix(8);
    else if (url.starts_with("http://")) out.secure = false, url.remove_prefix(7);
    else return false;
    const size_t slash = url.find('/');
    std::string_view hostport = url.substr(0, slash);
    out.path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    if (hostport.empty()) return false;
    out.port = out.secure ? 443 : 80;
    if (hostport.front() == '[') {   // [::1]:8099
        const size_t close = hostport.find(']');
        if (close == std::string_view::npos) return false;
        out.host = std::string(hostport.substr(1, close - 1));
        hostport.remove_prefix(close + 1);
        if (hostport.starts_with(':')) {
            int p = 0;
            if (!str::parse_int(hostport.substr(1), p) || p <= 0 || p > 65535) return false;
            out.port = u16(p);
        }
        return true;
    }
    const size_t colon = hostport.rfind(':');
    out.host = std::string(hostport.substr(0, colon));
    if (colon != std::string_view::npos) {
        int p = 0;
        if (!str::parse_int(hostport.substr(colon + 1), p) || p <= 0 || p > 65535) return false;
        out.port = u16(p);
    }
    return !out.host.empty();
}

bool loopback_host(std::string_view host) {
    return host == "127.0.0.1" || host == "::1" || str::iequals(host, "localhost") || host.starts_with("127.");
}

}  // namespace

std::string HttpResponse::header(std::string_view name) const {
    for (const auto& [k, v] : headers)
        if (str::iequals(k, name)) return v;
    return {};
}

void set_user_agent(std::string a) {
    std::lock_guard lock(g_agent_mutex);
    g_agent = std::move(a);
}

bool url_allowed(std::string_view url, std::string* why) {
    UrlParts u;
    if (!split_url(url, u)) {
        if (why) *why = "not a web address: " + std::string(url);
        return false;
    }
    if (!u.secure && !loopback_host(u.host)) {
        if (why) *why = "only https may be used off this machine: " + std::string(url);
        return false;
    }
    return true;
}

#ifdef _WIN32

HttpResponse http_request(const HttpRequest& req) {
    HttpResponse res;
    UrlParts u;
    if (std::string why; !url_allowed(req.url, &why)) {
        res.error = why;
        return res;
    }
    split_url(req.url, u);
    const std::wstring wagent = str::widen(agent());
    HINTERNET session = WinHttpOpen(wagent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(wagent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = "the system's web client could not start";
        return res;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    const int ms = int(std::clamp(req.timeout, 1.0, 300.0) * 1000.0);
    WinHttpSetTimeouts(session, ms, ms, ms, ms);
    HINTERNET connect = WinHttpConnect(session, str::widen(u.host).c_str(), u.port, 0);
    HINTERNET request = nullptr;
    auto close_all = [&] {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
    };
    auto fail = [&](const char* what) {
        const DWORD e = GetLastError();
        res.error = what;
        if (e == ERROR_WINHTTP_TIMEOUT) res.error += ": timed out";
        else if (e == ERROR_WINHTTP_CANNOT_CONNECT) res.error += ": could not connect";
        else if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) res.error += ": the name was not found";
        else if (e == ERROR_WINHTTP_SECURE_FAILURE || e == ERROR_WINHTTP_SECURE_INVALID_CA || e == ERROR_WINHTTP_SECURE_CERT_CN_INVALID ||
                 e == ERROR_WINHTTP_SECURE_CERT_DATE_INVALID || e == ERROR_WINHTTP_SECURE_INVALID_CERT || e == ERROR_WINHTTP_SECURE_CERT_REVOKED)
            res.error += ": the server's certificate did not check out";
        else if (e == ERROR_WINHTTP_CONNECTION_ERROR) res.error += ": the connection was dropped";
        else res.error += str::format(" (error %lu)", static_cast<unsigned long>(e));
        close_all();
        return res;
    };
    if (!connect) return fail("no connection");
    request = WinHttpOpenRequest(connect, str::widen(req.method).c_str(), str::widen(u.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) return fail("no request");
    // Never follow a redirect: TVAS answers where it is asked, or not at all.
    DWORD no_redirect = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirect, sizeof no_redirect);
    std::wstring headers;
    for (const auto& [k, v] : req.headers) headers += str::widen(k) + L": " + str::widen(v) + L"\r\n";
    if (!req.body.empty() && std::none_of(req.headers.begin(), req.headers.end(), [](const auto& h) { return str::iequals(h.first, "content-type"); }))
        headers += L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : DWORD(-1L),
                            req.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(req.body.data()), DWORD(req.body.size()), DWORD(req.body.size()), 0))
        return fail("the request could not be sent");
    if (!WinHttpReceiveResponse(request, nullptr)) return fail("no answer");
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    res.status = int(status);
    DWORD hbytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &hbytes, WINHTTP_NO_HEADER_INDEX);
    if (hbytes) {
        std::wstring raw(hbytes / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &hbytes, WINHTTP_NO_HEADER_INDEX)) {
            const std::string text = str::narrow(raw);
            for (std::string_view line : str::split(text, '\n')) {
                line = str::trim(line);
                const size_t colon = line.find(':');
                if (colon == std::string_view::npos) continue;
                res.headers.emplace_back(str::lower(str::trim(line.substr(0, colon))), std::string(str::trim(line.substr(colon + 1))));
            }
        }
    }
    constexpr size_t kMaxBody = 16u << 20;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) return fail("the answer was cut off");
        if (avail == 0) break;
        const size_t at = res.body.size();
        if (at + avail > kMaxBody) {
            res.error = "the answer was too large";
            close_all();
            return res;
        }
        res.body.resize(at + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request, res.body.data() + at, avail, &read)) return fail("the answer was cut off");
        res.body.resize(at + read);
    }
    close_all();
    return res;
}

#elif defined(__ANDROID__)

// The phone's own HTTPS (GameActivity.webRequest: HttpsURLConnection, the system's certificates).
// The activity's class is found once on the game's thread: a worker thread attached to Java sees
// only the system's classes, never the app's.
namespace {

JavaVM* g_vm = nullptr;
jclass g_activity_class = nullptr;
jmethodID g_web_request = nullptr;

// A worker thread is attached to Java on its first request and detached when it ends (a thread
// that ends attached takes the app down).
struct JavaThread {
    JNIEnv* env = nullptr;
    ~JavaThread() {
        if (env && g_vm) g_vm->DetachCurrentThread();
    }
};

JNIEnv* java_env() {
    thread_local JavaThread self;
    if (!self.env && g_vm && g_vm->AttachCurrentThread(&self.env, nullptr) != JNI_OK) self.env = nullptr;
    return self.env;
}

std::string from_java(JNIEnv* env, jstring s) {
    std::string out;
    if (!s) return out;
    if (const char* c = env->GetStringUTFChars(s, nullptr)) {
        out = c;
        env->ReleaseStringUTFChars(s, c);
    }
    return out;
}

}  // namespace

void android_web_init(void* java_vm, void* activity) {
    auto* vm = static_cast<JavaVM*>(java_vm);
    JNIEnv* env = nullptr;
    if (!vm || vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) return;
    jclass c = env->GetObjectClass(static_cast<jobject>(activity));
    g_web_request = env->GetStaticMethodID(c, "webRequest", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[BI[Ljava/lang/String;)[B");
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (g_web_request) g_activity_class = static_cast<jclass>(env->NewGlobalRef(c));
    env->DeleteLocalRef(c);
    g_vm = vm;
}

HttpResponse http_request(const HttpRequest& req) {
    HttpResponse res;
    if (std::string why; !url_allowed(req.url, &why)) {
        res.error = why;
        return res;
    }
    JNIEnv* env = java_env();
    // A worker thread lives long: every local reference this request makes goes with its frame.
    if (!env || !g_activity_class || !g_web_request || env->PushLocalFrame(32) != JNI_OK) {
        res.error = "the web client could not start";
        return res;
    }
    // Name, value, name, value...: the caller's, the game's agent, and JSON for a body not typed.
    std::vector<std::string> pairs;
    bool typed = false;
    for (const auto& [k, v] : req.headers) {
        pairs.push_back(k), pairs.push_back(v);
        typed |= str::iequals(k, "content-type");
    }
    if (!req.body.empty() && !typed) pairs.push_back("Content-Type"), pairs.push_back("application/json");
    pairs.push_back("User-Agent"), pairs.push_back(agent());

    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray headers = env->NewObjectArray(jsize(pairs.size()), string_class, nullptr);
    for (size_t i = 0; i < pairs.size(); ++i) {
        jstring s = env->NewStringUTF(pairs[i].c_str());
        env->SetObjectArrayElement(headers, jsize(i), s);
        env->DeleteLocalRef(s);
    }
    jbyteArray body = nullptr;
    if (!req.body.empty() || req.method == "POST") {
        body = env->NewByteArray(jsize(req.body.size()));
        env->SetByteArrayRegion(body, 0, jsize(req.body.size()), reinterpret_cast<const jbyte*>(req.body.data()));
    }
    jobjectArray answer = env->NewObjectArray(3, string_class, nullptr);   // status, headers, error
    jstring method = env->NewStringUTF(req.method.c_str());
    jstring url = env->NewStringUTF(req.url.c_str());
    const jint timeout_ms = jint(std::clamp(req.timeout, 1.0, 300.0) * 1000.0);

    auto reply = static_cast<jbyteArray>(env->CallStaticObjectMethod(g_activity_class, g_web_request, method, url, headers, body, timeout_ms, answer));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        res.error = "the web client failed";
    } else {
        const std::string status = from_java(env, static_cast<jstring>(env->GetObjectArrayElement(answer, 0)));
        const std::string lines = from_java(env, static_cast<jstring>(env->GetObjectArrayElement(answer, 1)));
        res.error = from_java(env, static_cast<jstring>(env->GetObjectArrayElement(answer, 2)));
        if (res.error.empty()) {
            if (!str::parse_int(status, res.status)) res.error = "no answer";
            for (std::string_view rest = lines; !rest.empty();) {
                const size_t end = rest.find('\n');
                const std::string_view line = rest.substr(0, end);
                rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
                if (const size_t colon = line.find(':'); colon != std::string_view::npos)
                    res.headers.emplace_back(str::lower(str::trim(line.substr(0, colon))), std::string(str::trim(line.substr(colon + 1))));
            }
            if (reply) {
                const jsize n = env->GetArrayLength(reply);
                res.body.resize(size_t(n));
                env->GetByteArrayRegion(reply, 0, n, reinterpret_cast<jbyte*>(res.body.data()));
            }
        }
    }
    env->PopLocalFrame(nullptr);
    return res;
}

#else

// The system's libcurl, opened when first needed: every desktop and server distribution has it, as
// has every Mac (/usr/lib/libcurl.4.dylib); it uses the system's TLS and certificate store, and
// nothing has to be shipped beside the program.
namespace {

using CurlHandle = void;
struct CurlApi {
    bool loaded = false;
    std::string why;
    int (*global_init)(long) = nullptr;
    CurlHandle* (*easy_init)() = nullptr;
    int (*easy_setopt)(CurlHandle*, int, ...) = nullptr;
    int (*easy_perform)(CurlHandle*) = nullptr;
    int (*easy_getinfo)(CurlHandle*, int, ...) = nullptr;
    void (*easy_cleanup)(CurlHandle*) = nullptr;
    const char* (*easy_strerror)(int) = nullptr;
    void* (*slist_append)(void*, const char*) = nullptr;
    void (*slist_free_all)(void*) = nullptr;
};

// libcurl's own numbers (curl/curl.h): CURLOPTTYPE_LONG 0, OBJECTPOINT 10000, FUNCTIONPOINT 20000.
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptPostFields = 10015, kOptUserAgent = 10018, kOptHttpHeader = 10023,
              kOptHeaderData = 10029, kOptCustomRequest = 10036, kOptWriteFunction = 20011, kOptHeaderFunction = 20079, kOptPostFieldSize = 60,
              kOptNoSignal = 99, kOptTimeoutMs = 155, kOptConnectTimeoutMs = 156, kOptFollowLocation = 52, kOptSslVerifyPeer = 64, kOptSslVerifyHost = 81;
constexpr int kInfoResponseCode = 0x200002;

CurlApi& curl() {
    static CurlApi api = [] {
        CurlApi a;
        void* lib = nullptr;
#ifdef __APPLE__
        for (const char* name : {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"})
#else
        for (const char* name : {"libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4", "libcurl-nss.so.4"})
#endif
            if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
        if (!lib) {
            a.why = "libcurl is not installed (the system package curl or libcurl4 provides it)";
            return a;
        }
        auto sym = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name)); return fn != nullptr; };
        if (!(sym(a.global_init, "curl_global_init") && sym(a.easy_init, "curl_easy_init") && sym(a.easy_setopt, "curl_easy_setopt") &&
              sym(a.easy_perform, "curl_easy_perform") && sym(a.easy_getinfo, "curl_easy_getinfo") && sym(a.easy_cleanup, "curl_easy_cleanup") &&
              sym(a.easy_strerror, "curl_easy_strerror") && sym(a.slist_append, "curl_slist_append") && sym(a.slist_free_all, "curl_slist_free_all"))) {
            a.why = "this libcurl is missing functions the game needs";
            return a;
        }
        a.global_init(3);   // CURL_GLOBAL_DEFAULT
        a.loaded = true;
        return a;
    }();
    return api;
}

size_t on_body(char* data, size_t size, size_t n, void* user) {
    auto* res = static_cast<HttpResponse*>(user);
    const size_t bytes = size * n;
    if (res->body.size() + bytes > (16u << 20)) return 0;
    res->body.append(data, bytes);
    return bytes;
}

size_t on_header(char* data, size_t size, size_t n, void* user) {
    auto* res = static_cast<HttpResponse*>(user);
    const std::string_view line = str::trim(std::string_view(data, size * n));
    if (const size_t colon = line.find(':'); colon != std::string_view::npos)
        res->headers.emplace_back(str::lower(str::trim(line.substr(0, colon))), std::string(str::trim(line.substr(colon + 1))));
    return size * n;
}

}  // namespace

HttpResponse http_request(const HttpRequest& req) {
    HttpResponse res;
    if (std::string why; !url_allowed(req.url, &why)) {
        res.error = why;
        return res;
    }
    CurlApi& c = curl();
    if (!c.loaded) {
        res.error = c.why;
        return res;
    }
    CurlHandle* h = c.easy_init();
    if (!h) {
        res.error = "the web client could not start";
        return res;
    }
    void* headers = nullptr;
    bool typed = false;
    for (const auto& [k, v] : req.headers) {
        headers = c.slist_append(headers, (k + ": " + v).c_str());
        typed |= str::iequals(k, "content-type");
    }
    if (!req.body.empty() && !typed) headers = c.slist_append(headers, "Content-Type: application/json");
    const std::string ua = agent();
    c.easy_setopt(h, kOptUrl, req.url.c_str());
    c.easy_setopt(h, kOptUserAgent, ua.c_str());
    c.easy_setopt(h, kOptNoSignal, 1L);
    c.easy_setopt(h, kOptFollowLocation, 0L);
    c.easy_setopt(h, kOptSslVerifyPeer, 1L);
    c.easy_setopt(h, kOptSslVerifyHost, 2L);
    c.easy_setopt(h, kOptTimeoutMs, long(std::clamp(req.timeout, 1.0, 300.0) * 1000.0));
    c.easy_setopt(h, kOptConnectTimeoutMs, long(std::clamp(req.timeout, 1.0, 300.0) * 1000.0));
    c.easy_setopt(h, kOptWriteFunction, &on_body);
    c.easy_setopt(h, kOptWriteData, &res);
    c.easy_setopt(h, kOptHeaderFunction, &on_header);
    c.easy_setopt(h, kOptHeaderData, &res);
    if (headers) c.easy_setopt(h, kOptHttpHeader, headers);
    if (req.method != "GET") c.easy_setopt(h, kOptCustomRequest, req.method.c_str());
    if (!req.body.empty() || req.method == "POST") {
        c.easy_setopt(h, kOptPostFields, req.body.c_str());
        c.easy_setopt(h, kOptPostFieldSize, long(req.body.size()));
    }
    const int rc = c.easy_perform(h);
    if (rc != 0) {
        res.error = c.easy_strerror(rc);
        if (rc == 60 || rc == 51 || rc == 35) res.error = "the server's certificate did not check out (" + res.error + ")";
    } else {
        long code = 0;
        c.easy_getinfo(h, kInfoResponseCode, &code);
        res.status = int(code);
    }
    if (headers) c.slist_free_all(headers);
    c.easy_cleanup(h);
    return res;
}

#endif

// ── HttpWorker ─────────────────────────────────────────────────────────────────

HttpWorker::HttpWorker(int threads) {
    for (int i = 0; i < std::max(1, threads); ++i) threads_.emplace_back([this] { run(); });
}

HttpWorker::~HttpWorker() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        // Requests not yet started are dropped: closing never waits on a queue of them (each up
        // to its timeout). What must not be lost is kept elsewhere first (a server's match reports
        // on disk, PR-7); a request already under way is still waited for.
        queue_.clear();
    }
    wake_.notify_all();
    for (std::thread& t : threads_)
        if (t.joinable()) t.join();
}

void HttpWorker::submit(HttpRequest request, Callback done) {
    {
        std::lock_guard lock(mutex_);
        queue_.push_back(Job{std::move(request), std::move(done), {}});
    }
    wake_.notify_one();
}

void HttpWorker::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            ++in_flight_;
        }
        job.response = http_request(job.request);
        std::lock_guard lock(mutex_);
        --in_flight_;
        finished_.push_back(std::move(job));
    }
}

void HttpWorker::poll() {
    std::deque<Job> done;
    {
        std::lock_guard lock(mutex_);
        done.swap(finished_);
    }
    for (Job& j : done)
        if (j.done) j.done(std::move(j.response));
}

size_t HttpWorker::pending() const {
    std::lock_guard lock(mutex_);
    return queue_.size() + in_flight_ + finished_.size();
}

void HttpWorker::drain(double seconds) {
    const double end = time::now() + seconds;
    while (time::now() < end) {
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty() && in_flight_ == 0) break;
        }
        time::sleep_precise(0.005);
    }
    poll();
}

}  // namespace eng::net
