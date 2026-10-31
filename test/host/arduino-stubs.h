#pragma once
// Minimal host substitutes for ESP32 I/O; production JSON and firmware code are used unchanged.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define F(x) x
#define PROGMEM
#define ESP_ARDUINO_VERSION_MAJOR 3
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string& s) : std::string(s) {}
    String(int n) : std::string(std::to_string(n)) {}
    String(float n) : std::string(std::to_string(n)) {}
    String(double n, int = 2) : std::string(std::to_string(n)) {}
    String(long n, int) : std::string(std::to_string(n)) {}
    int indexOf(char c) const { auto p=find(c); return p==npos ? -1 : int(p); }
    int toInt() const { return std::atoi(c_str()); }
    void trim() { const char* space=" \t\r\n"; erase(0,find_first_not_of(space)); erase(find_last_not_of(space)+1); }
    size_t write(uint8_t c) { push_back(char(c)); return 1; }
    size_t write(const uint8_t* p, size_t n) { append((const char*)p,n); return n; }
    size_t cursor = 0;
    int read() { return cursor < size() ? uint8_t((*this)[cursor++]) : -1; }
    size_t readBytes(char* p, size_t n) { n=std::min(n,size()-cursor); memcpy(p,data()+cursor,n); cursor+=n; return n; }
};
struct SerialMock {
    std::string input;
    explicit operator bool() const { return true; }
    int available() { return input.size(); }
    String readStringUntil(char end) {
        const auto p = input.find(end);
        String line = input.substr(0, p);
        input.erase(0, p == std::string::npos ? p : p + 1);
        return line;
    }
    template<class... T> void println(T...) {}
    template<class... T> void print(T...) {}
    template<class... T> void printf(T...) {}
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
} Serial;
struct ESPMock { int restarts = 0; void restart() { ++restarts; } } ESP;
inline uint32_t mockMillis = 0;
inline unsigned long millis() { return mockMillis; }
inline void delay(int) {}

// Connectivity substitutes exercise the real MQTT loop's fallback paths.
using byte = uint8_t;
constexpr int WL_CONNECTED = 3, HEX = 16;
inline long random(long maximum) { return maximum - 1; }
struct IPAddressMock { String toString() const { return "192.0.2.10"; } };
struct WiFiMock {
    int connectionStatus = WL_CONNECTED;
    int status() { return connectionStatus; }
    String macAddress() { return "AA:BB:CC:0D:0E:0F"; }
    IPAddressMock localIP() { return {}; }
} WiFi;
struct WiFiClient {
    uint32_t connectTimeout = 0;
    void setConnectionTimeout(uint32_t milliseconds) { connectTimeout = milliseconds; }
};
struct WiFiClientSecure : WiFiClient {
    bool insecure = false;
    unsigned long handshakeTimeout = 0;
    void setInsecure() { insecure = true; }
    void setHandshakeTimeout(unsigned long seconds) { handshakeTimeout = seconds; }
};
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
};
class Stream : public Print {
public:
    virtual int read() = 0;
    virtual int available() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
};
struct Published { std::string topic, payload; bool retained; };
struct PubSubClient {
    bool isConnected = true, connectResult = false, dropDuringLoop = false;
    int failureState = -1, connects = 0;
    std::function<void()> beforeConnect;
    std::function<void(char *, uint8_t *, unsigned int)> callback;
    void *client = nullptr;
    std::string host, clientId, user, password, willTopic, willMessage;
    int port = 0, willQos = -1;
    bool willRetain = false;
    std::vector<std::string> subscriptions;
    std::vector<Published> published;
    Published pending;
    template<class C> explicit PubSubClient(C &c) : client(&c) {}
    template<class C> PubSubClient &setClient(C &c) { client = &c; return *this; }
    bool connected() { return isConnected; }
    bool connect(const char *id, const char *u, const char *p) { return connect(id, u, p, nullptr, 0, false, nullptr); }
    bool connect(const char *id, const char *u, const char *p, const char *wt, uint8_t wq, bool wr, const char *wm) {
        ++connects;
        clientId = id; user = u ? u : ""; password = p ? p : "";
        willTopic = wt ? wt : ""; willQos = wq; willRetain = wr; willMessage = wm ? wm : "";
        if (beforeConnect) beforeConnect();
        return isConnected = connectResult;
    }
    int state() { return isConnected ? 0 : failureState; }
    bool loop() { if (dropDuringLoop) isConnected = false; return isConnected; }
    void disconnect() { isConnected = false; failureState = -1; }
    bool subscribe(const char *topic) { subscriptions.push_back(topic); return true; }
    bool beginPublish(const char *topic, unsigned int, bool retained) {
        if (!isConnected) return false;
        pending = {topic, "", retained};
        return true;
    }
    size_t write(const uint8_t *data, size_t length) { pending.payload.append((const char *)data, length); return length; }
    int endPublish() { published.push_back(pending); return 1; }
    PubSubClient &setServer(const char *h, uint16_t p) { host = h; port = p; return *this; }
    void setCallback(std::function<void(char *, uint8_t *, unsigned int)> handler) { callback = handler; }
    template<class... T> void setBufferSize(T...) {}
    template<class T> void setStream(T &) {}
    template<class... T> void setSocketTimeout(T...) {}
    template<class... T> void setKeepAlive(T...) {}
    // Simulates the broker delivering a message during loop().
    void deliver(const std::string &topic, const std::string &payload) {
        std::vector<char> name(topic.begin(), topic.end());
        name.push_back('\0');
        std::vector<uint8_t> body(payload.begin(), payload.end());
        callback(name.data(), body.data(), body.size());
    }
};
inline bool failOpen=false, failRename=false, shortRead=false;
inline size_t writeBudget = SIZE_MAX;
class File {
    std::shared_ptr<std::string> contents;
    size_t cursor=0;
public:
    File() = default;
    File(std::shared_ptr<std::string> p) : contents(p) {}
    explicit operator bool() const { return bool(contents); }
    size_t size() { return contents->size(); }
    size_t readBytes(char* p,size_t n) { n=std::min(n,size()-cursor); if (shortRead && n) --n; memcpy(p,contents->data()+cursor,n); cursor+=n; return n; }
    size_t write(const uint8_t* p,size_t n) { n=std::min(n,writeBudget); contents->append((const char*)p,n); writeBudget-=n; return n; }
    size_t write(uint8_t c) { return write(&c,1); }
    void close() { contents.reset(); }
};
struct LittleFSMock {
    std::map<std::string,std::shared_ptr<std::string>> files;
    File open(const char* path,const char* mode) {
        if (*mode=='w') {
            if (failOpen) return {};
            files[path]=std::make_shared<std::string>();
        }
        auto it=files.find(path);
        return it==files.end() ? File() : File(it->second);
    }
    bool rename(const char* from,const char* to) {
        if (failRename || !files.count(from)) return false;
        files[to]=files[from]; files.erase(from); return true;
    }
    bool remove(const char* p) { return files.erase(p)>0; }
    bool exists(const char* p) { return files.count(p)>0; }
    bool begin() { return true; }
    void format() { files.clear(); }
} LittleFS;
constexpr int HTTP_POST=1, HTTP_GET=2;
constexpr int UPLOAD_FILE_START=1, UPLOAD_FILE_WRITE=2, UPLOAD_FILE_END=3, UPLOAD_FILE_ABORTED=4;
enum HTTPAuthMethod { BASIC_AUTH, DIGEST_AUTH };
constexpr size_t UPDATE_SIZE_UNKNOWN=0;
struct HTTPUpload { int status=0; String filename; uint8_t* buf=nullptr; size_t currentSize=0,totalSize=0; };
struct WebServer {
    std::map<std::string,String> params;
    std::vector<int> responses;
    WebServer(int) {}
    int args() { return params.size(); }
    String arg(const char* key) { auto it=params.find(key); return it==params.end() ? String() : it->second; }
    bool hasArg(const char* key) { return params.count(key); }
    String lastBody;
    // Request headers, and the login the client sends in its Authorization header.
    std::map<std::string,String> headers;
    std::pair<std::string,std::string> credentials;
    int authenticationRequests = 0;
    void send(int status,const char* = "",const String& body = "") { responses.push_back(status); lastBody=body; }
    template<class... T> void sendHeader(T...) {}
    template<class... T> void send_P(int status, T...) { responses.push_back(status); }
    template<class... T> void on(T...) {}
    template<class... T> void collectHeaders(T...) {}
    bool hasHeader(const char* name) { return headers.count(name) && !headers[name].empty(); }
    bool authenticate(const char* user,const char* password) { return credentials.first == user && credentials.second == password; }
    void requestAuthentication(HTTPAuthMethod,const char*,const String& body) {
        ++authenticationRequests; send(401,"text/html",body);
    }
    void begin() {}
    void handleClient() {}
    HTTPUpload& upload() { static HTTPUpload u; return u; }
};
struct MDNSMock { bool begin(const char*) { return true; } } MDNS;
struct UpdateMock {
    bool began = false, ended = false, aborted = false, fail = false;
    size_t written = 0;
    bool hasError() { return fail; }
    bool isFinished() { return ended; }
    bool begin(size_t) { began = true; return true; }
    size_t write(uint8_t*,size_t n) { written += n; return n; }
    bool end(bool) { ended = !fail; return ended; }
    void abort() { aborted = true; began = false; }
    template<class T> void printError(T&) {}
} Update;

constexpr int OUTPUT=1;
inline std::map<int,int> pwmValues;
inline void pinMode(int,int) {}
inline void analogWrite(int pin,int duty) { pwmValues[pin]=duty; }
inline long map(long value,long a,long b,long c,long d) { return (value-a)*(d-c)/(b-a)+c; }
