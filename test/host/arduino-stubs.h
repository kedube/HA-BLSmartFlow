#pragma once
// Minimal host substitutes for ESP32 I/O; production JSON and firmware code are used unchanged.
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
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
    String(unsigned int n) : std::string(std::to_string(n)) {}
    String(unsigned long n) : std::string(std::to_string(n)) {}
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
struct ESPMock {
    int restarts = 0;
    void restart() { ++restarts; }
    uint32_t getFreeHeap() { return 150000; }
    uint32_t getMinFreeHeap() { return 90000; }
    uint32_t getMaxAllocHeap() { return 65524; }
} ESP;
enum esp_reset_reason_t { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO };
inline esp_reset_reason_t mockResetReason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return mockResetReason; }
inline int64_t mockUptimeMicros = 0;
inline int64_t esp_timer_get_time() { return mockUptimeMicros; }
inline uint32_t mockMillis = 0;
inline unsigned long millis() { return mockMillis; }
inline void delay(int) {}

// Connectivity substitutes exercise the real MQTT loop's fallback paths.
using byte = uint8_t;
constexpr int WL_CONNECTED = 3, HEX = 16;
inline long random(long maximum) { return maximum - 1; }
struct IPAddress {
    uint8_t octets[4] = {192, 0, 2, 10};
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : octets{a, b, c, d} {}
    String toString() const {
        return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
            std::to_string(octets[2]) + "." + std::to_string(octets[3]);
    }
};
using IPAddressMock = IPAddress;
enum wifi_power_t : int8_t { WIFI_POWER_19_5dBm = 78, WIFI_POWER_2dBm = 8 };
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM };
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK };
constexpr int16_t WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2;
struct ScannedNetwork { std::string ssid; int8_t rssi; wifi_auth_mode_t auth; };
struct WiFiMock {
    int connectionStatus = WL_CONNECTED;
    int txPower = WIFI_POWER_19_5dBm, radioChanges = 0;
    bool sleep = true;
    // Setup network and station attempts.
    wifi_mode_t wifiMode = WIFI_STA;
    std::string apName, joinedSsid, joinedPassword;
    IPAddress apAddress;
    int stations = 0, joins = 0, disconnects = 0;
    bool autoReconnect = true;
    // Scans: results stay until deleted; scanState is the count, or running/failed.
    std::vector<ScannedNetwork> networks;
    int16_t scanState = WIFI_SCAN_FAILED, startResult = WIFI_SCAN_RUNNING;
    int scansStarted = 0, scansDeleted = 0;
    int status() { return connectionStatus; }
    String macAddress() { return "AA:BB:CC:0D:0E:0F"; }
    IPAddressMock localIP() { return {}; }
    String SSID() { return "test-network"; }
    String SSID(int i) { return networks.at(i).ssid; }
    String BSSIDstr() { return "02:00:00:00:00:01"; }
    int32_t channel() { return 6; }
    int8_t RSSI() { return -61; }
    int8_t RSSI(int i) { return networks.at(i).rssi; }
    wifi_auth_mode_t encryptionType(int i) { return networks.at(i).auth; }
    int16_t scanNetworks(bool) { ++scansStarted; return scanState = startResult; }
    int16_t scanComplete() { return scanState; }
    void scanDelete() { ++scansDeleted; scanState = WIFI_SCAN_FAILED; }
    bool mode(wifi_mode_t m) { wifiMode = m; return true; }
    bool softAP(const char *name) { apName = name; return true; }
    bool softAPConfig(IPAddress local, IPAddress, IPAddress) { apAddress = local; return true; }
    uint8_t softAPgetStationNum() { return stations; }
    bool setAutoReconnect(bool enabled) { autoReconnect = enabled; return true; }
    bool disconnect() { ++disconnects; return true; }
    int begin(const char *ssid, const char *password) { ++joins; joinedSsid = ssid; joinedPassword = password; return 0; }
    bool setTxPower(wifi_power_t power) { txPower = power; ++radioChanges; return true; }
    wifi_power_t getTxPower() { return static_cast<wifi_power_t>(txPower); }
    bool setSleep(bool enabled) { sleep = enabled; ++radioChanges; return true; }
    wifi_ps_type_t getSleep() { return sleep ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE; }
} WiFi;
struct DNSServer {
    bool started = false;
    std::string domain;
    IPAddress address;
    int requests = 0;
    bool start(uint16_t, const String &name, const IPAddress &ip) { started = true; domain = name; address = ip; return true; }
    void processNextRequest() { ++requests; }
    void stop() { started = false; }
};
struct UdpPacket { std::string payload; IPAddress sender; };
struct WiFiUDP {
    bool listenResult = true, listening = false;
    IPAddress group;
    uint16_t port = 0;
    std::vector<UdpPacket> incoming;
    UdpPacket current;
    std::vector<std::string> sent;
    std::string outgoing;
    uint8_t beginMulticast(IPAddress ip, uint16_t p) { group = ip; port = p; return listening = listenResult; }
    void stop() { listening = false; }
    int parsePacket() {
        if (incoming.empty()) return 0;
        current = incoming.front();
        incoming.erase(incoming.begin());
        return current.payload.size();
    }
    int read(char *buffer, size_t length) {
        const size_t copied = std::min(length, current.payload.size());
        memcpy(buffer, current.payload.data(), copied);
        return copied;
    }
    IPAddress remoteIP() { return current.sender; }
    int beginPacket(IPAddress, uint16_t) { outgoing.clear(); return 1; }
    size_t write(const uint8_t *data, size_t length) { outgoing.append((const char *)data, length); return length; }
    int endPacket() { sent.push_back(outgoing); return 1; }
};
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
// Formats like Arduino's Print, so text logged through LogSerial can be checked.
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *data, size_t length) {
        size_t written = 0;
        while (length--) written += write(*data++);
        return written;
    }
    size_t print(const char *text) { return write(reinterpret_cast<const uint8_t *>(text), strlen(text)); }
    size_t print(const std::string &text) { return write(reinterpret_cast<const uint8_t *>(text.data()), text.size()); }
    size_t print(char c) { return write(uint8_t(c)); }
    template<class T, class = std::enable_if_t<std::is_arithmetic_v<T>>> size_t print(T value) { return print(std::to_string(value)); }
    size_t println() { return print("\r\n"); }
    template<class T> size_t println(const T &value) { return print(value) + println(); }
    size_t printf(const char *format, ...) {
        char text[256];
        va_list arguments;
        va_start(arguments, format);
        const int length = vsnprintf(text, sizeof(text), format, arguments);
        va_end(arguments);
        return length < 0 ? 0 : print(std::string(text, std::min<size_t>(length, sizeof(text) - 1)));
    }
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
    std::map<std::string,String> responseHeaders;
    void send(int status,const char* = "",const String& body = "") { responses.push_back(status); lastBody=body; }
    template<class N, class V> void sendHeader(const N& name, const V& value, bool = false) { responseHeaders[String(name)] = String(value); }
    template<class... T> void send_P(int status, T...) { responses.push_back(status); }
    template<class... T> void on(T...) {}
    template<class F> void onNotFound(F) {}
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
struct MDNSMock {
    std::string hostname;
    int starts = 0;
    bool begin(const char *name) { hostname = name; ++starts; return true; }
    void end() { hostname.clear(); }
} MDNS;
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

// ESP32SSDP: counts starts and stops of the discovery responder.
struct SSDPMock {
    int begins = 0, ends = 0;
    template<class... T> void setSchemaURL(T...) {}
    template<class... T> void setHTTPPort(T...) {}
    template<class... T> void setDeviceType(T...) {}
    template<class... T> void setName(T...) {}
    template<class... T> void setSerialNumber(T...) {}
    template<class... T> void setURL(T...) {}
    template<class... T> void setModelName(T...) {}
    template<class... T> void setModelNumber(T...) {}
    template<class... T> void setManufacturer(T...) {}
    template<class... T> void setManufacturerURL(T...) {}
    bool begin() { ++begins; return true; }
    void end() { ++ends; }
} SSDP;
