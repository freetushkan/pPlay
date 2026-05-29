#include "dlna_renderer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

#include "utility.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pplay {

namespace {

constexpr const char *SSDP_ADDR = "239.255.255.250";
constexpr uint16_t SSDP_PORT = 1900;
constexpr const char *AVT_SERVICE = "urn:schemas-upnp-org:service:AVTransport:1";
constexpr const char *RC_SERVICE = "urn:schemas-upnp-org:service:RenderingControl:1";
constexpr const char *MR_DEVICE = "urn:schemas-upnp-org:device:MediaRenderer:1";

static std::string xmlEscape(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (char c: value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c; break;
        }
    }
    return out;
}

static std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return (char) std::toupper(c);
    });
    return value;
}

static bool containsNoCase(const std::string &haystack, const std::string &needle) {
    return toUpper(haystack).find(toUpper(needle)) != std::string::npos;
}

static std::string extractBetween(const std::string &text, const std::string &left, const std::string &right) {
    size_t start = text.find(left);
    if (start == std::string::npos) return "";
    start += left.size();
    size_t end = text.find(right, start);
    if (end == std::string::npos) return "";
    return text.substr(start, end - start);
}

static std::string extractTagValue(const std::string &xml, const std::string &tag) {
    std::string value = extractBetween(xml, "<" + tag + ">", "</" + tag + ">");
    if (!value.empty()) return value;

    size_t tagStart = xml.find("<" + tag);
    if (tagStart == std::string::npos) return "";
    size_t valueStart = xml.find('>', tagStart);
    if (valueStart == std::string::npos) return "";
    valueStart++;
    size_t valueEnd = xml.find("</" + tag + ">", valueStart);
    if (valueEnd == std::string::npos) return "";
    return xml.substr(valueStart, valueEnd - valueStart);
}

static long parseTimeToSeconds(const std::string &value) {
    int h = 0, m = 0;
    double s = 0;
    if (std::sscanf(value.c_str(), "%d:%d:%lf", &h, &m, &s) == 3) {
        return h * 3600 + m * 60 + (long) s;
    }
    return std::strtol(value.c_str(), nullptr, 10);
}

static std::string secondsToTime(long seconds) {
    if (seconds < 0) seconds = 0;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << (seconds / 3600) << ':'
        << std::setw(2) << ((seconds / 60) % 60) << ':'
        << std::setw(2) << (seconds % 60);
    return oss.str();
}

static std::string randomUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 15);
    std::uniform_int_distribution<int> dis2(8, 11);
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << '-';
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << '-';
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << '-';
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

#if !defined(_WIN32)
static void closeSocket(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

static bool sendAll(int fd, const std::string &data) {
    const char *ptr = data.c_str();
    size_t left = data.size();
    while (left > 0) {
        ssize_t sent = send(fd, ptr, left, 0);
        if (sent <= 0) return false;
        ptr += sent;
        left -= (size_t) sent;
    }
    return true;
}
#endif

} // namespace

DlnaRenderer::DlnaRenderer(const std::string &name, uint16_t port)
    : friendlyName(name), uuid(randomUuid()), requestedPort(port) {
}

DlnaRenderer::~DlnaRenderer() {
    stop();
}

bool DlnaRenderer::start() {
#if defined(_WIN32)
    return false;
#else
    if (running) return true;

    httpSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (httpSocket < 0) return false;
    int yes = 1;
    setsockopt(httpSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in httpAddr{};
    httpAddr.sin_family = AF_INET;
    httpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    httpAddr.sin_port = htons(requestedPort);
    if (bind(httpSocket, (sockaddr *) &httpAddr, sizeof(httpAddr)) < 0 || listen(httpSocket, 8) < 0) {
        closeSocket(httpSocket);
        return false;
    }

    socklen_t len = sizeof(httpAddr);
    if (getsockname(httpSocket, (sockaddr *) &httpAddr, &len) == 0) {
        httpPort = ntohs(httpAddr.sin_port);
    }

    ssdpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ssdpSocket < 0) {
        closeSocket(httpSocket);
        return false;
    }
    setsockopt(ssdpSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in ssdpAddr{};
    ssdpAddr.sin_family = AF_INET;
    ssdpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    ssdpAddr.sin_port = htons(SSDP_PORT);
    if (bind(ssdpSocket, (sockaddr *) &ssdpAddr, sizeof(ssdpAddr)) < 0) {
        closeSocket(ssdpSocket);
        closeSocket(httpSocket);
        return false;
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(ssdpSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    running = true;
    httpThread = std::thread(&DlnaRenderer::httpLoop, this);
    ssdpThread = std::thread(&DlnaRenderer::ssdpLoop, this);
    sendNotify("ssdp:alive");
    Utility::log(Utility::LogLevel::Info, "DLNA renderer started at " + locationUrl());
    return true;
#endif
}

void DlnaRenderer::stop() {
#if !defined(_WIN32)
    if (!running) return;
    sendNotify("ssdp:byebye");
    running = false;
    closeSocket(ssdpSocket);
    closeSocket(httpSocket);
    if (ssdpThread.joinable()) ssdpThread.join();
    if (httpThread.joinable()) httpThread.join();
#endif
}

bool DlnaRenderer::isRunning() const {
    return running;
}

uint16_t DlnaRenderer::getHttpPort() const {
    return httpPort;
}

void DlnaRenderer::updateState(const State &newState) {
    std::lock_guard<std::mutex> lock(stateMutex);
    state = newState;
}

bool DlnaRenderer::popCommand(Command &command) {
    std::lock_guard<std::mutex> lock(commandMutex);
    if (commands.empty()) return false;
    command = commands.front();
    commands.pop();
    return true;
}

void DlnaRenderer::enqueue(const Command &command) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commands.push(command);
}

std::string DlnaRenderer::localIp() const {
#if defined(_WIN32)
    return "127.0.0.1";
#else
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return "127.0.0.1";
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_ADDR, &addr.sin_addr);
    connect(fd, (sockaddr *) &addr, sizeof(addr));
    sockaddr_in name{};
    socklen_t len = sizeof(name);
    std::string ip = "127.0.0.1";
    if (getsockname(fd, (sockaddr *) &name, &len) == 0) {
        char buffer[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &name.sin_addr, buffer, sizeof(buffer))) {
            ip = buffer;
        }
    }
    close(fd);
    return ip;
#endif
}

std::string DlnaRenderer::locationUrl() const {
    return "http://" + localIp() + ":" + std::to_string(httpPort) + "/description.xml";
}

void DlnaRenderer::sendNotify(const std::string &nts) {
#if !defined(_WIN32)
    if (httpPort == 0) return;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    int ttl = 2;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_ADDR, &addr.sin_addr);

    std::vector<std::string> targets = {"upnp:rootdevice", "uuid:" + uuid, MR_DEVICE, AVT_SERVICE, RC_SERVICE};
    for (const auto &target: targets) {
        std::ostringstream msg;
        msg << "NOTIFY * HTTP/1.1\r\n"
            << "HOST: " << SSDP_ADDR << ":" << SSDP_PORT << "\r\n"
            << "CACHE-CONTROL: max-age=1800\r\n"
            << "LOCATION: " << locationUrl() << "\r\n"
            << "NT: " << target << "\r\n"
            << "NTS: " << nts << "\r\n"
            << "SERVER: pPlay/4 UPnP/1.0 DLNADOC/1.50\r\n"
            << "USN: uuid:" << uuid << (target == "uuid:" + uuid ? "" : "::" + target) << "\r\n\r\n";
        std::string data = msg.str();
        sendto(fd, data.c_str(), data.size(), 0, (sockaddr *) &addr, sizeof(addr));
    }
    close(fd);
#endif
}

void DlnaRenderer::respondToSearch(const std::string &request, const std::string &remoteIp, uint16_t remotePort) {
#if !defined(_WIN32)
    if (!containsNoCase(request, "M-SEARCH") || !containsNoCase(request, "ssdp:discover")) return;
    std::string st = "upnp:rootdevice";
    if (containsNoCase(request, AVT_SERVICE)) st = AVT_SERVICE;
    else if (containsNoCase(request, RC_SERVICE)) st = RC_SERVICE;
    else if (containsNoCase(request, MR_DEVICE)) st = MR_DEVICE;
    else if (containsNoCase(request, "uuid:" + uuid)) st = "uuid:" + uuid;
    else if (!containsNoCase(request, "ssdp:all") && !containsNoCase(request, "upnp:rootdevice")) return;

    std::ostringstream msg;
    msg << "HTTP/1.1 200 OK\r\n"
        << "CACHE-CONTROL: max-age=1800\r\n"
        << "EXT:\r\n"
        << "LOCATION: " << locationUrl() << "\r\n"
        << "SERVER: pPlay/4 UPnP/1.0 DLNADOC/1.50\r\n"
        << "ST: " << st << "\r\n"
        << "USN: uuid:" << uuid << (st == "uuid:" + uuid ? "" : "::" + st) << "\r\n\r\n";

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(remotePort);
    inet_pton(AF_INET, remoteIp.c_str(), &remote.sin_addr);
    std::string data = msg.str();
    sendto(ssdpSocket, data.c_str(), data.size(), 0, (sockaddr *) &remote, sizeof(remote));
#endif
}

void DlnaRenderer::ssdpLoop() {
#if !defined(_WIN32)
    auto nextNotify = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (running) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(ssdpSocket, &set);
        timeval tv{1, 0};
        int res = select(ssdpSocket + 1, &set, nullptr, nullptr, &tv);
        if (res > 0 && FD_ISSET(ssdpSocket, &set)) {
            char buffer[2048] = {};
            sockaddr_in remote{};
            socklen_t len = sizeof(remote);
            ssize_t received = recvfrom(ssdpSocket, buffer, sizeof(buffer) - 1, 0, (sockaddr *) &remote, &len);
            if (received > 0) {
                char ip[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &remote.sin_addr, ip, sizeof(ip));
                respondToSearch(std::string(buffer, (size_t) received), ip, ntohs(remote.sin_port));
            }
        }
        if (std::chrono::steady_clock::now() >= nextNotify) {
            sendNotify("ssdp:alive");
            nextNotify = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        }
    }
#endif
}

void DlnaRenderer::httpLoop() {
#if !defined(_WIN32)
    while (running) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(httpSocket, &set);
        timeval tv{1, 0};
        int res = select(httpSocket + 1, &set, nullptr, nullptr, &tv);
        if (res <= 0 || !FD_ISSET(httpSocket, &set)) continue;
        int client = accept(httpSocket, nullptr, nullptr);
        if (client >= 0) {
            handleHttpClient(client);
            close(client);
        }
    }
#endif
}

void DlnaRenderer::handleHttpClient(int clientFd) {
#if !defined(_WIN32)
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 1024 * 1024) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n <= 0) return;
        request.append(buffer, (size_t) n);
    }
    size_t headerEnd = request.find("\r\n\r\n");
    size_t contentLength = 0;
    std::string headers = request.substr(0, headerEnd);
    std::istringstream hs(headers);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string upper = toUpper(line);
        if (upper.rfind("CONTENT-LENGTH:", 0) == 0) {
            contentLength = (size_t) std::strtoul(line.substr(15).c_str(), nullptr, 10);
        }
    }
    while (request.size() < headerEnd + 4 + contentLength) {
        ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        request.append(buffer, (size_t) n);
    }

    std::istringstream firstLine(request.substr(0, request.find("\r\n")));
    std::string method, path;
    firstLine >> method >> path;

    std::string body;
    std::string contentType = "text/xml; charset=\"utf-8\"";
    int status = 200;
    std::string reason = "OK";
    if (method == "GET" && path == "/description.xml") {
        body = buildDescriptionXml();
    } else if (method == "GET" && path == "/AVTransport/scpd.xml") {
        body = buildAvTransportScpdXml();
    } else if (method == "GET" && path == "/RenderingControl/scpd.xml") {
        body = buildRenderingControlScpdXml();
    } else if (method == "POST" && (path == "/AVTransport/control" || path == "/RenderingControl/control")) {
        body = handleSoap(path, request);
    } else {
        status = 404;
        reason = "Not Found";
        contentType = "text/plain";
        body = "Not Found";
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "CONTENT-TYPE: " << contentType << "\r\n"
             << "CONTENT-LENGTH: " << body.size() << "\r\n"
             << "CONNECTION: close\r\n\r\n" << body;
    sendAll(clientFd, response.str());
#endif
}

std::string DlnaRenderer::buildDescriptionXml() const {
    return "<?xml version=\"1.0\"?>"
        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
        "<specVersion><major>1</major><minor>0</minor></specVersion>"
        "<device>"
        "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>"
        "<friendlyName>" + xmlEscape(friendlyName) + "</friendlyName>"
        "<manufacturer>pPlay</manufacturer><modelName>pPlay DLNA Renderer</modelName>"
        "<UDN>uuid:" + uuid + "</UDN>"
        "<serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
        "<serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>"
        "<SCPDURL>/AVTransport/scpd.xml</SCPDURL><controlURL>/AVTransport/control</controlURL><eventSubURL>/AVTransport/event</eventSubURL></service>"
        "<service><serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
        "<serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>"
        "<SCPDURL>/RenderingControl/scpd.xml</SCPDURL><controlURL>/RenderingControl/control</controlURL><eventSubURL>/RenderingControl/event</eventSubURL></service>"
        "</serviceList></device></root>";
}

std::string DlnaRenderer::buildAvTransportScpdXml() const {
    return "<?xml version=\"1.0\"?>"
        "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<actionList>"
        "<action><name>SetAVTransportURI</name></action><action><name>Play</name></action><action><name>Pause</name></action>"
        "<action><name>Stop</name></action><action><name>Seek</name></action><action><name>GetTransportInfo</name></action>"
        "<action><name>GetPositionInfo</name></action><action><name>GetMediaInfo</name></action><action><name>GetDeviceCapabilities</name></action>"
        "</actionList><serviceStateTable>"
        "<stateVariable sendEvents=\"yes\"><name>TransportState</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>TransportStatus</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>CurrentURI</name><dataType>string</dataType></stateVariable>"
        "<stateVariable sendEvents=\"no\"><name>RelativeTimePosition</name><dataType>string</dataType></stateVariable>"
        "</serviceStateTable></scpd>";
}

std::string DlnaRenderer::buildRenderingControlScpdXml() const {
    return "<?xml version=\"1.0\"?>"
        "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<actionList><action><name>GetVolume</name></action><action><name>SetVolume</name></action></actionList>"
        "<serviceStateTable><stateVariable sendEvents=\"yes\"><name>Volume</name><dataType>ui2</dataType></stateVariable>"
        "<allowedValueRange><minimum>0</minimum><maximum>100</maximum><step>1</step></allowedValueRange>"
        "</serviceStateTable></scpd>";
}

std::string DlnaRenderer::buildSoapResponse(const std::string &service, const std::string &action, const std::string &body) const {
    return "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:" + action + "Response xmlns:u=\"" + service + "\">" + body +
        "</u:" + action + "Response></s:Body></s:Envelope>";
}

std::string DlnaRenderer::buildSoapError(int code, const std::string &description) const {
    return "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault>"
        "<faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
        "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>" + std::to_string(code) +
        "</errorCode><errorDescription>" + xmlEscape(description) + "</errorDescription></UPnPError>"
        "</detail></s:Fault></s:Body></s:Envelope>";
}

std::string DlnaRenderer::handleSoap(const std::string &path, const std::string &request) {
    State current;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        current = state;
    }
    const bool avTransport = path.find("AVTransport") != std::string::npos;
    const std::string service = avTransport ? AVT_SERVICE : RC_SERVICE;

    if (containsNoCase(request, "SetAVTransportURI")) {
        std::string uri = extractTagValue(request, "CurrentURI");
        if (!uri.empty()) enqueue({CommandType::LoadUri, uri, 0});
        return buildSoapResponse(service, "SetAVTransportURI", "");
    }
    if (containsNoCase(request, "Play")) {
        enqueue({CommandType::Play, "", 0});
        return buildSoapResponse(service, "Play", "");
    }
    if (containsNoCase(request, "Pause")) {
        enqueue({CommandType::Pause, "", 0});
        return buildSoapResponse(service, "Pause", "");
    }
    if (containsNoCase(request, "Stop")) {
        enqueue({CommandType::Stop, "", 0});
        return buildSoapResponse(service, "Stop", "");
    }
    if (containsNoCase(request, "Seek")) {
        std::string unit = extractTagValue(request, "Unit");
        std::string target = extractTagValue(request, "Target");
        if (unit == "REL_TIME" || unit == "ABS_TIME") {
            enqueue({CommandType::SeekAbsolute, "", (double) parseTimeToSeconds(target)});
        } else if (unit == "X_DLNA_REL_BYTE" || unit == "REL_COUNT") {
            enqueue({CommandType::SeekRelative, "", (double) std::strtol(target.c_str(), nullptr, 10)});
        }
        return buildSoapResponse(service, "Seek", "");
    }
    if (containsNoCase(request, "SetVolume")) {
        int volume = std::atoi(extractTagValue(request, "DesiredVolume").c_str());
        volume = std::max(0, std::min(100, volume));
        enqueue({CommandType::SetVolume, "", (double) volume});
        return buildSoapResponse(service, "SetVolume", "");
    }
    if (containsNoCase(request, "GetVolume")) {
        return buildSoapResponse(service, "GetVolume", "<CurrentVolume>" + std::to_string(current.volume) + "</CurrentVolume>");
    }
    if (containsNoCase(request, "GetTransportInfo")) {
        std::string stateName = current.stopped ? "STOPPED" : (current.paused ? "PAUSED_PLAYBACK" : "PLAYING");
        return buildSoapResponse(service, "GetTransportInfo",
            "<CurrentTransportState>" + stateName + "</CurrentTransportState>"
            "<CurrentTransportStatus>OK</CurrentTransportStatus><CurrentSpeed>1</CurrentSpeed>");
    }
    if (containsNoCase(request, "GetPositionInfo")) {
        std::string duration = secondsToTime(current.duration);
        std::string position = secondsToTime(current.position);
        return buildSoapResponse(service, "GetPositionInfo",
            "<Track>1</Track><TrackDuration>" + duration + "</TrackDuration><TrackMetaData></TrackMetaData>"
            "<TrackURI>" + xmlEscape(current.uri) + "</TrackURI><RelTime>" + position + "</RelTime>"
            "<AbsTime>" + position + "</AbsTime><RelCount>2147483647</RelCount><AbsCount>2147483647</AbsCount>");
    }
    if (containsNoCase(request, "GetMediaInfo")) {
        return buildSoapResponse(service, "GetMediaInfo",
            "<NrTracks>1</NrTracks><MediaDuration>" + secondsToTime(current.duration) + "</MediaDuration>"
            "<CurrentURI>" + xmlEscape(current.uri) + "</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>"
            "<NextURI></NextURI><NextURIMetaData></NextURIMetaData><PlayMedium>NETWORK</PlayMedium>"
            "<RecordMedium>NOT_IMPLEMENTED</RecordMedium><WriteStatus>NOT_IMPLEMENTED</WriteStatus>");
    }
    if (containsNoCase(request, "GetDeviceCapabilities")) {
        return buildSoapResponse(service, "GetDeviceCapabilities",
            "<PlayMedia>NETWORK</PlayMedia><RecMedia>NOT_IMPLEMENTED</RecMedia><RecQualityModes>NOT_IMPLEMENTED</RecQualityModes>");
    }

    return buildSoapError(401, "Invalid Action");
}

} // namespace pplay
