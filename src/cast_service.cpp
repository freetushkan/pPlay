#include "cast_service.h"

#include "cast_creds_data.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <array>

#include "cross2d/c2d.h"
#include "pplay_config.h"
#include "player.h"
#include "utility.h"
#include "main.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

// openscreen
#include "cast/standalone_receiver/cast_service.h"
#include "cast/receiver/channel/device_auth_namespace_handler.h"
#include "cast/common/channel/message_util.h"
#include "cast/common/channel/proto/cast_channel.pb.h"
#include "cast/common/channel/virtual_connection.h"
#include "cast/common/channel/virtual_connection_router.h"
#include "platform/api/network_interface.h"
#include "platform/api/time.h"
#include "platform/base/error.h"
#include "platform/base/span.h"
#include "platform/base/ip_address.h"
#include "platform/impl/logging.h"
#include "platform/impl/network_interface.h"
#include "platform/impl/platform_client_posix.h"
#include "platform/impl/task_runner.h"
#include "util/chrono_helpers.h"
#include "util/string_util.h"
#include "util/stringprintf.h"
#include "util/uuid.h"
#include "util/crypto/certificate_utils.h"

#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/bytestring.h>
#include <openssl/asn1.h>
#include <openssl/digest.h>
#include <stddef.h>
#include <stdint.h>

#include <pthread.h>
#include <cstdint>
#include <string_view>

extern "C" {
    int RSA_private_key_to_bytes(uint8_t **out_bytes, size_t *out_len, const RSA *rsa) {
        if (!out_bytes || !out_len || !rsa) return 0;
        *out_bytes = nullptr;
        *out_len = 0;
        int len = i2d_RSAPrivateKey(rsa, nullptr);
        if (len <= 0) return 0;
        uint8_t *buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(len)));
        if (!buffer) return 0;
        uint8_t *cursor = buffer;
        if (i2d_RSAPrivateKey(rsa, &cursor) != len) {
            OPENSSL_free(buffer);
            return 0;
        }
        *out_bytes = buffer;
        *out_len = static_cast<size_t>(len);
        return 1;
    }
    int RSA_public_key_to_bytes(uint8_t **out_bytes, size_t *out_len, const RSA *rsa) {
        if (!out_bytes || !out_len || !rsa) return 0;
        *out_bytes = nullptr;
        *out_len = 0;
        int len = i2d_RSA_PUBKEY(const_cast<RSA*>(rsa), nullptr);
        if (len <= 0) return 0;
        uint8_t *buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(len)));
        if (!buffer) return 0;
        uint8_t *cursor = buffer;
        if (i2d_RSA_PUBKEY(const_cast<RSA*>(rsa), &cursor) != len) {
            OPENSSL_free(buffer);
            return 0;
        }
        *out_bytes = buffer;
        *out_len = static_cast<size_t>(len);
        return 1;
    }
    void EVP_cleanup(void) {}
    void X509V3_EXT_free(void *ext) {}
    int EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx) { return 1; }
    void CRYPTO_library_init(void) {}
    long SSL_CTX_set_mode(void* ctx, long mode) { return mode; }
    void AES_ctr128_encrypt(const uint8_t* in, uint8_t* out, size_t len, const void* key,
                            uint8_t* ivec, uint8_t* ecount_buf, unsigned int* num) {}
    RSA *RSA_private_key_from_bytes(const uint8_t *bytes, size_t len) {
        const uint8_t *p = bytes;
        return d2i_RSAPrivateKey(nullptr, &p, len);
    }
    int X509_set_notBefore(X509 *x, const ASN1_TIME *tm) {
        ASN1_TIME *current = X509_get_notBefore(x);
        if (!current) return 0;
        return ASN1_STRING_copy(current, tm) ? 1 : 0;
    }
    int X509_set_notAfter(X509 *x, const ASN1_TIME *tm) {
        ASN1_TIME *current = X509_get_notAfter(x);
        if (!current) return 0;
        return ASN1_STRING_copy(current, tm) ? 1 : 0;
    }
    int EVP_PKEY_assign_RSA(EVP_PKEY *pkey, RSA *key) {
        return EVP_PKEY_set1_RSA(pkey, key);
    }
    int EVP_DigestSignUpdate(EVP_MD_CTX *ctx, const void *data, size_t dsize) {
        return EVP_DigestUpdate(ctx, data, dsize);
    }
    long SSL_CTX_set_session_cache_mode(SSL_CTX *ctx, long mode) { return mode; }
    void ps4_evp_md_ctx_init_hook(EVP_MD_CTX *ctx) __asm__("EVP_MD_CTX_init");
    void ps4_evp_md_ctx_init_hook(EVP_MD_CTX *ctx) {
        if (ctx) { std::memset(ctx, 0, sizeof(EVP_MD_CTX)); }
    }
    int getentropy(void *buf, size_t buflen) {
        if (buflen > 256) { return -1; }
        int fd = open("/dev/random", O_RDONLY | O_CLOEXEC);
        if (fd < 0) { return -1; }
        size_t total_read = 0;
        uint8_t *p = static_cast<uint8_t*>(buf);
        while (total_read < buflen) {
            ssize_t bytes_read = read(fd, p + total_read, buflen - total_read);
            if (bytes_read < 0) {
                close(fd);
                return -1;
            }
            total_read += bytes_read;
        }
        close(fd);
        return 0;
    }
    void __gcov_init(void* info) {}
    void __gcov_dump(void) {}
    void __gcov_flush(void) {}

    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetIoctl(int s, unsigned long com, void *data);
    int sceNetGetsockname(int s, struct sockaddr *name, unsigned int *namelen);
    int sceNetSocketClose(int s, int how);
    int sceNetGetMacAddress(uint8_t *mac_out, int if_index);
    int getifaddrs(struct ifaddrs **ifap) {
        if (!ifap) return -1;
        *ifap = nullptr;
        int sock = sceNetSocket("pplay_net_query", AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return -1;
        struct sockaddr_in loopback_trigger{};
        loopback_trigger.sin_family = AF_INET;
        loopback_trigger.sin_port = htons(53);
        loopback_trigger.sin_addr.s_addr = inet_addr("8.8.8.8");
        connect(sock, (struct sockaddr*)&loopback_trigger, sizeof(loopback_trigger));
        struct sockaddr_in local_bound_address{};
        unsigned int namelen = sizeof(local_bound_address);
        struct sockaddr_in* ip_addr = (struct sockaddr_in*)std::malloc(sizeof(struct sockaddr_in));
        std::memset(ip_addr, 0, sizeof(struct sockaddr_in));
        ip_addr->sin_family = AF_INET;
        if (sceNetGetsockname(sock, (struct sockaddr*)&local_bound_address, &namelen) == 0 && 
            local_bound_address.sin_addr.s_addr != 0) {
            ip_addr->sin_addr.s_addr = local_bound_address.sin_addr.s_addr;
        } else {
            ip_addr->sin_addr.s_addr = inet_addr("127.0.0.1");
        }
        const char* picked_ifname = "sce_net0";
        unsigned int flags = IFF_UP | IFF_RUNNING;
        struct ifreq ifr{};
        std::strncpy(ifr.ifr_name, "sce_net0", sizeof(ifr.ifr_name) - 1);
        if (sceNetIoctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
            if (!(ifr.ifr_flags & IFF_UP) || !(ifr.ifr_flags & IFF_RUNNING)) {
                picked_ifname = "sce_net1";
            } else {
                flags = ifr.ifr_flags;
            }
        }
        struct sockaddr_in* netmask_addr = (struct sockaddr_in*)std::malloc(sizeof(struct sockaddr_in));
        std::memset(netmask_addr, 0, sizeof(struct sockaddr_in));
        netmask_addr->sin_family = AF_INET;
        std::strncpy(ifr.ifr_name, picked_ifname, sizeof(ifr.ifr_name) - 1);
        if (sceNetIoctl(sock, SIOCGIFNETMASK, &ifr) >= 0 && ifr.ifr_netmask.sa_family == AF_INET) {
            struct sockaddr_in* real_mask = (struct sockaddr_in*)&ifr.ifr_netmask;
            netmask_addr->sin_addr.s_addr = real_mask->sin_addr.s_addr;
        } else {
            netmask_addr->sin_addr.s_addr = inet_addr("255.255.255.0");
        }
        sceNetSocketClose(sock, 0);
        struct ifaddrs *new_if = (struct ifaddrs *)std::malloc(sizeof(struct ifaddrs));
        std::memset(new_if, 0, sizeof(struct ifaddrs));
        new_if->ifa_name = strdup(picked_ifname);
        new_if->ifa_flags = flags | IFF_UP | IFF_RUNNING;
        new_if->ifa_addr = (struct sockaddr *)ip_addr;
        new_if->ifa_netmask = (struct sockaddr *)netmask_addr;
        *ifap = new_if;
        return 0;
    }
    void freeifaddrs(struct ifaddrs *ifa) {
        while (ifa) {
            struct ifaddrs *next = ifa->ifa_next;
            if (ifa->ifa_name) std::free(ifa->ifa_name);
            if (ifa->ifa_addr) std::free(ifa->ifa_addr);
            if (ifa->ifa_netmask) std::free(ifa->ifa_netmask);
            std::free(ifa);
            ifa = next;
        }
    }
}

// absl compatibility
#include "absl/base/internal/low_level_alloc.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/strings/internal/cord_rep_crc.h"
#include "absl/crc/internal/crc_cord_state.h"
struct AbslThreadSem {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
};

void AbslInternalPerThreadSemPost(absl::base_internal::ThreadIdentity* identity) {
    static AbslThreadSem sem;
    static bool inited = false;
    if (!inited) {
        pthread_mutex_init(&sem.mutex, nullptr);
        pthread_cond_init(&sem.cond, nullptr);
        sem.count = 0;
        inited = true;
    }
    pthread_mutex_lock(&sem.mutex);
    sem.count++;
    pthread_cond_signal(&sem.cond);
    pthread_mutex_unlock(&sem.mutex);
}
bool AbslInternalPerThreadSemWait(absl::synchronization_internal::KernelTimeout timeout) {
    static AbslThreadSem sem;
    pthread_mutex_lock(&sem.mutex);
    while (sem.count <= 0) {
        pthread_cond_wait(&sem.cond, &sem.mutex);
    }
    sem.count--;
    pthread_mutex_unlock(&sem.mutex);
    return true;
}

namespace absl {
    namespace cord_internal {
        CordRepCrc* CordRepCrc::New(CordRep* head, crc_internal::CrcCordState crc_state) {
            void* node = std::malloc(48);
            if (!node) return reinterpret_cast<CordRepCrc*>(head);
            std::memset(node, 0, 48);
            return static_cast<CordRepCrc*>(node);
        }
        void CordRepCrc::Destroy(CordRepCrc* node) {
            std::free(node);
        }
    }
    namespace base_internal {
        void* LowLevelAlloc::Alloc(unsigned long size) { return std::malloc(size); }
        void LowLevelAlloc::Free(void* ptr) { std::free(ptr); }
    }
    namespace synchronization_internal {
        base_internal::ThreadIdentity* CreateThreadIdentity() {
            static uint64_t dummy_id = 0xABCDE;
            return (base_internal::ThreadIdentity*)(&dummy_id);
        }
    }
    namespace container_internal {
        void ForcedTrySample(void*) {}
        bool SampleEverything(void*) { return false; }
    }
    namespace crc_internal { int TryNewCRC32AcceleratedX86ARMCombined() { return 0; } }
    namespace status_internal { void* GetStatusPayloadPrinter() { return nullptr; } }
}



#include <sys/ioctl.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <algorithm>
#include <string>
#include <vector>
#include <array>

#include "platform/api/network_interface.h"
#include "platform/base/ip_address.h"
#include "platform/base/span.h"

// openscreen ps4 network impl
namespace openscreen {
    namespace {
        uint8_t ToPrefixLength(std::span<const uint8_t> netmask) {
            uint8_t result = 0;
            size_t i = 0;
            while (i < netmask.size() && netmask[i] == UINT8_C(0xff)) {
                result += 8;
                ++i;
            }
            if (i < netmask.size() && netmask[i] != UINT8_C(0x00)) {
                uint8_t last_byte = netmask[i];
                while (last_byte & UINT8_C(0x80)) {
                    ++result;
                    last_byte <<= 1;
                }
                ++i;
            }
            return result;
        }
        IPAddress GetIPAddressFromSockAddr(const sockaddr_in& sa) {
            uint32_t s_addr = sa.sin_addr.s_addr;
            uint8_t b1 = s_addr & 0xFF;
            uint8_t b2 = (s_addr >> 8) & 0xFF;
            uint8_t b3 = (s_addr >> 16) & 0xFF;
            uint8_t b4 = (s_addr >> 24) & 0xFF;
            return IPAddress(b1, b2, b3, b4);
        }
        void GetHardwareAddress(const std::string& if_name, uint8_t* mac_out) {
            std::memset(mac_out, 0, 6);
            int if_index = 0;
            if (if_name == "sce_net0") {
                if_index = 1;
            } else if (if_name == "sce_net1") {
                if_index = 2;
            } else {
                return;
            }
            sceNetGetMacAddress(mac_out, if_index);
        }
        std::vector<InterfaceInfo> ProcessInterfacesList(ifaddrs* interfaces) {
            std::vector<InterfaceInfo> results;
            for (ifaddrs* cur = interfaces; cur; cur = cur->ifa_next) {
                if (!(IFF_RUNNING & cur->ifa_flags) || !cur->ifa_addr) { continue; }
                if (cur->ifa_addr->sa_family != AF_INET) { continue; }
                const std::string name = cur->ifa_name;
                auto it = std::find_if(results.begin(), results.end(),
                    [&name](const InterfaceInfo& info) { return info.name == name; });
                InterfaceInfo* interface;
                if (it == results.end()) {
                    InterfaceInfo::Type type = InterfaceInfo::Type::kOther;
                    if (name == "sce_net0") {
                        type = InterfaceInfo::Type::kEthernet;
                    } else if (name == "sce_net1") {
                        type = InterfaceInfo::Type::kWifi;
                    } else if (cur->ifa_flags & IFF_LOOPBACK) {
                        type = InterfaceInfo::Type::kLoopback;
                    } else {
                        continue;
                    }
                    uint8_t hardware_address[6] = {0, 0, 0, 0, 0, 0};
                    GetHardwareAddress(name, hardware_address);
                    results.emplace_back(if_nametoindex(cur->ifa_name),
                                        hardware_address, name, type,
                                        std::vector<IPSubnet>());
                    interface = &(results.back());
                } else {
                    interface = &(*it);
                }
                auto* const addr_in = reinterpret_cast<const sockaddr_in*>(cur->ifa_addr);
                IPAddress ip = GetIPAddressFromSockAddr(*addr_in);
                std::array<uint8_t, IPAddress::kV4Size> netmask_bytes{};
                if (cur->ifa_netmask && cur->ifa_netmask->sa_family == AF_INET) {
                    auto* netmask_in = reinterpret_cast<const sockaddr_in*>(cur->ifa_netmask);
                    std::copy_n(reinterpret_cast<const uint8_t*>(&netmask_in->sin_addr.s_addr),
                                netmask_bytes.size(), netmask_bytes.begin());
                }
                interface->addresses.emplace_back(ip, ToPrefixLength(netmask_bytes));
            }
            return results;
        }
    }  // namespace
    std::vector<InterfaceInfo> GetNetworkInterfaces() {
        std::vector<InterfaceInfo> results;
        ifaddrs* interfaces;
        if (getifaddrs(&interfaces) == 0) {
            results = ProcessInterfacesList(interfaces);
            freeifaddrs(interfaces);
        }
        return results;
    }
}  // namespace openscreen



#include <google/protobuf/message_lite.h>
#include <string_view>
#include <string>

namespace google {
    namespace protobuf {
        bool MessageLite::ParseFromString(std::string_view input) {
            return ParseFromString(std::string(input.data(), input.size()));
        }
        bool MessageLite::SerializeToString(std::string* output) const {
            return AppendToString(output);
        }

    }  // namespace protobuf
}  // namespace google

using namespace pplay;


// AirReceiver-compatible discovery needs to replay the public Google-signed
// device certificate chain and the matching two-day precomputed signature
// for the deterministic TLS certificate instead.
namespace openscreen {
namespace cast {

namespace {

using openscreen::cast::channel::AuthChallenge;
using openscreen::cast::channel::AuthError;
using openscreen::cast::channel::AuthResponse;
using openscreen::cast::channel::CastMessage;
using openscreen::cast::channel::DeviceAuthMessage;
using openscreen::cast::channel::HashAlgorithm;
using openscreen::cast::channel::SignatureAlgorithm;

CastMessage GenerateAuthErrorMessage(AuthError::ErrorType error_type) {
    DeviceAuthMessage message;
    AuthError* error = message.mutable_error();
    error->set_error_type(error_type);

    std::string payload;
    message.SerializeToString(&payload);

    CastMessage response;
    response.set_protocol_version(openscreen::cast::channel::CastMessage_ProtocolVersion_CASTV2_1_0);
    response.set_namespace_(kAuthNamespace);
    response.set_payload_type(openscreen::cast::channel::CastMessage_PayloadType_BINARY);
    response.set_payload_binary(std::move(payload));
    return response;
}

std::size_t CurrentCredsSignatureOffset() {
    using namespace std::chrono;
    const auto now = GetWallTimeSinceUnixEpoch();
    const auto start = seconds(pplay::cast::creds::kSignatureStartUnixSeconds);
    int64_t index = 0;
    if (now > start) {
        index = duration_cast<seconds>(now - start).count() /
                pplay::cast::creds::kSignaturePeriodSeconds;
    }
    const int64_t signature_count = static_cast<int64_t>(
        pplay::cast::creds::kSignaturesLen / pplay::cast::creds::kSignatureSize);
    if (index < 0) index = 0;
    if (index >= signature_count) index = signature_count - 1;
    return static_cast<std::size_t>(index) * pplay::cast::creds::kSignatureSize;
}

}  // namespace

DeviceAuthNamespaceHandler::CredentialsProvider::~CredentialsProvider() = default;

DeviceAuthNamespaceHandler::DeviceAuthNamespaceHandler(CredentialsProvider& creds_provider)
    : creds_provider_(creds_provider) {}

DeviceAuthNamespaceHandler::~DeviceAuthNamespaceHandler() = default;

void DeviceAuthNamespaceHandler::OnMessage(VirtualConnectionRouter* router,
                                           CastSocket* socket,
                                           CastMessage message) {
    if (!socket || !creds_provider_) {
        return;
    }
    if (message.payload_type() != openscreen::cast::channel::CastMessage_PayloadType_BINARY) {
        return;
    }

    const std::string& payload = message.payload_binary();
    DeviceAuthMessage device_auth_message;
    if (!device_auth_message.ParseFromString(payload) ||
        !device_auth_message.has_challenge() ||
        device_auth_message.has_response() ||
        device_auth_message.has_error()) {
        return;
    }

    const VirtualConnection virtual_conn{
        message.destination_id(), message.source_id(), socket->socket_id()};
    const AuthChallenge& challenge = device_auth_message.challenge();
    const SignatureAlgorithm sig_alg = challenge.signature_algorithm();
    const HashAlgorithm hash_alg = challenge.hash_algorithm();

    if ((sig_alg != openscreen::cast::channel::UNSPECIFIED &&
         sig_alg != openscreen::cast::channel::RSASSA_PKCS1v15) ||
        (hash_alg != openscreen::cast::channel::SHA1 && hash_alg != openscreen::cast::channel::SHA256)) {
        router->Send(virtual_conn,
                     GenerateAuthErrorMessage(AuthError::SIGNATURE_ALGORITHM_UNAVAILABLE));
        return;
    }

    const auto tls_cert_der = creds_provider_->GetCurrentTlsCertAsDer();
    const DeviceCredentials& device_creds = creds_provider_->GetCurrentDeviceCredentials();
    if (tls_cert_der.empty() || device_creds.certs.empty()) {
        router->Send(virtual_conn, GenerateAuthErrorMessage(AuthError::INTERNAL_ERROR));
        return;
    }

    std::unique_ptr<AuthResponse> auth_response(new AuthResponse());
    auth_response->set_client_auth_certificate(std::string(
        reinterpret_cast<const char*>(pplay::cast::creds::kAuthCrt),
        pplay::cast::creds::kAuthCrtLen));
    auth_response->add_intermediate_certificate(std::string(
        reinterpret_cast<const char*>(pplay::cast::creds::kIntermediateCrt),
        pplay::cast::creds::kIntermediateCrtLen));
    auth_response->set_signature_algorithm(openscreen::cast::channel::RSASSA_PKCS1v15);
    auth_response->set_hash_algorithm(hash_alg);
    auth_response->set_crl(device_creds.serialized_crl);

    const std::size_t offset = CurrentCredsSignatureOffset();
    auth_response->set_signature(std::string(
        reinterpret_cast<const char*>(&pplay::cast::creds::kSignatures[offset]),
        pplay::cast::creds::kSignatureSize));

    DeviceAuthMessage response_auth_message;
    response_auth_message.set_allocated_response(auth_response.release());

    std::string response_string;
    response_auth_message.SerializeToString(&response_string);

    CastMessage response;
    response.set_protocol_version(openscreen::cast::channel::CastMessage_ProtocolVersion_CASTV2_1_0);
    response.set_namespace_(kAuthNamespace);
    response.set_payload_type(openscreen::cast::channel::CastMessage_PayloadType_BINARY);
    response.set_payload_binary(std::move(response_string));

    router->Send(virtual_conn, std::move(response));
}

}  // namespace cast
}  // namespace openscreen

namespace {

using openscreen::cast::CastService;
using openscreen::cast::GeneratedCredentials;
using openscreen::InterfaceInfo;
using openscreen::cast::StaticCredentialsProvider;
using openscreen::cast::GenerateCredentials;
using openscreen::cast::GenerateCredentialsForTesting;
using openscreen::GetNetworkInterfaces;
using openscreen::TaskRunnerImpl;
using openscreen::PlatformClientPosix;


// Credential bytes live in cast_creds_data.h.

// ==================== HELPER FUNCTIONS ====================

void log_info(const std::string &message) {
    Utility::log(Utility::LogLevel::Info, "CastService: " + message);
}

void closeSocket(int fd) {
    if (fd >= 0) close(fd);
}

bool setReuseAddr(int fd) {
    int yes = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0;
}

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string urlDecode(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            char *end = nullptr;
            long decoded = std::strtol(hex, &end, 16);
            if (end && *end == 0) {
                out.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

std::string queryParam(const std::string &target, const std::string &name) {
    size_t question = target.find('?');
    if (question == std::string::npos) return "";
    std::string query = target.substr(question + 1);
    size_t start = 0;
    while (start <= query.size()) {
        size_t amp = query.find('&', start);
        std::string part = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        size_t eq = part.find('=');
        std::string key = urlDecode(part.substr(0, eq));
        if (key == name) {
            return urlDecode(eq == std::string::npos ? "" : part.substr(eq + 1));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return "";
}

std::string xmlEscape(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (char ch: value) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string jsonEscape(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (char ch: value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string httpResponse(const std::string &body, const std::string &contentType,
                            const std::string &extraHeaders = "") {
    std::ostringstream ss;
    ss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << extraHeaders
        << "Connection: close\r\n\r\n"
        << body;
    return ss.str();
}

std::string notFoundResponse() {
    std::string body = "Not found";
    std::ostringstream ss;
    ss << "HTTP/1.1 404 Not Found\r\nContent-Length: " << body.size()
        << "\r\nConnection: close\r\n\r\n" << body;
    return ss.str();
}

std::string okTextResponse(const std::string &body = "OK") {
    return httpResponse(body, "text/plain; charset=utf-8");
}

std::string headerValue(const std::string &request, const std::string &header) {
    std::string needle = lower(header) + ":";
    std::istringstream stream(request);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string low = lower(line);
        if (low.rfind(needle, 0) == 0) {
            std::string value = line.substr(needle.size());
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            return value;
        }
    }
    return "";
}

std::vector<std::string> splitRequestLine(const std::string &request) {
    size_t end = request.find("\r\n");
    std::istringstream ss(request.substr(0, end));
    std::vector<std::string> parts;
    std::string part;
    while (ss >> part) parts.push_back(part);
    return parts;
}

std::string pickInterfaceName() {
    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return "";

    std::string selected;
    for (auto *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        selected = ifa->ifa_name;
        break;
    }
    freeifaddrs(ifaddr);
    return selected;
}

InterfaceInfo findInterfaceInfo(const std::string &name) {
    if (name.empty()) {
        log_info("Missing interface name");
        return InterfaceInfo{};
    }
    std::vector<InterfaceInfo> interfaces = GetNetworkInterfaces();
    for (auto &iface : interfaces) {
        if (iface.name == name) {
            return iface;
        }
    }
    log_info("Invalid interface '" + name + "' specified. Available interfaces: ");
    for (auto &iface : interfaces) {
        log_info("  - " + iface.name);
    }
    return InterfaceInfo{};
}

std::string chooseFriendlyName(Main* main) {
    std::string name = main->getConfig()->getOption(OPT_CAST_RECEIVER_NAME)->getString();
    return name.empty() ? "pPlay" : name;
}

std::string chooseCredentialId(const std::string &receiverName, int httpPort) {
    std::string seed = receiverName + ":" + std::to_string(httpPort);
    std::string hash = Utility::md5hash(seed);
    return hash.substr(0, 8) + "-" + hash.substr(8, 4) + "-" + hash.substr(12, 4) + "-"
           + hash.substr(16, 4) + "-" + hash.substr(20, 12);
}

// ==================== CAST SERVICE THREAD ====================

struct ReceiverRuntime {
    std::mutex mutex;
    TaskRunnerImpl* runner = nullptr;
    bool serviceCreated = false;
    bool stopRequested = false;
};

ReceiverRuntime g_receiverRuntime;

void runCastServiceOnThread(const std::string &interfaceName,
                            const std::string &friendlyName,
                            const std::string &modelName,
                            bool enableDiscovery,
                            bool enableDscp,
                            const std::string &deviceId) {
    using namespace openscreen;
    using namespace openscreen::cast;
    using namespace std::chrono;

    InterfaceInfo interface = findInterfaceInfo(interfaceName);
    if (!(interface.GetIpAddressV4() || interface.GetIpAddressV6())) {
        log_info("ERROR: No IP address on interface " + interfaceName);
        return;
    }
    if (interface.GetIpAddressV4()) {
        std::stringstream ss;
        ss << interface.GetIpAddressV4(); 
        log_info("Interface " + interfaceName + " IPv4: " + ss.str());
    }
    if (interface.GetIpAddressV6()) {
        std::stringstream ss;
        ss << interface.GetIpAddressV6();
        log_info("Interface " + interfaceName + " IPv6: " + ss.str());
    }
    auto buildHardcodedCredentials = [&]() -> ErrorOr<GeneratedCredentials> {
        const unsigned char* key_ptr = pplay::cast::creds::kPeerKeyDer;
        std::unique_ptr<RSA, decltype(&RSA_free)> rsa(
            d2i_RSAPrivateKey(nullptr, &key_ptr, pplay::cast::creds::kPeerKeyDerLen),
            &RSA_free);
        if (!rsa) {
            return Error(Error::Code::kParseError, "Failed to parse embedded TLS key");
        }

        bssl::UniquePtr<EVP_PKEY> tls_key(EVP_PKEY_new());
        if (!tls_key || EVP_PKEY_set1_RSA(tls_key.get(), rsa.get()) != 1) {
            return Error(Error::Code::kParseError, "Failed to import embedded TLS key");
        }

        constexpr auto kCertificateDuration = std::chrono::seconds(
            pplay::cast::creds::kSignaturePeriodSeconds);
        const auto now = GetWallTimeSinceUnixEpoch();
        const auto startDate = std::chrono::seconds(
            pplay::cast::creds::kSignatureStartUnixSeconds);
        int64_t index = 0;
        if (now > startDate) {
            index = std::chrono::duration_cast<std::chrono::seconds>(now - startDate).count() /
                    pplay::cast::creds::kSignaturePeriodSeconds;
        }
        const int64_t signatureCount = static_cast<int64_t>(
            pplay::cast::creds::kSignaturesLen / pplay::cast::creds::kSignatureSize);
        if (index < 0) index = 0;
        if (index >= signatureCount) index = signatureCount - 1;
        const auto certDate = startDate + std::chrono::seconds(
            index * pplay::cast::creds::kSignaturePeriodSeconds);

        ErrorOr<bssl::UniquePtr<X509>> tls_cert_or_error =
            CreateSelfSignedX509Certificate(pplay::cast::creds::kTlsCertificateName,
                                            kCertificateDuration, *tls_key, certDate);
        if (!tls_cert_or_error.is_value()) {
            return tls_cert_or_error.error();
        }
        bssl::UniquePtr<X509> tls_cert = std::move(tls_cert_or_error.value());

        const RSA* rsa_key = EVP_PKEY_get0_RSA(tls_key.get());
        size_t key_len = 0;
        uint8_t* key_bytes = nullptr;
        if (!RSA_private_key_to_bytes(&key_bytes, &key_len, rsa_key) || key_len == 0) {
            return Error(Error::Code::kParseError, "Failed to serialize embedded TLS private key");
        }
        std::vector<uint8_t> tls_key_der(key_bytes, key_bytes + key_len);
        OPENSSL_free(key_bytes);

        key_len = 0;
        key_bytes = nullptr;
        if (!RSA_public_key_to_bytes(&key_bytes, &key_len, rsa_key) || key_len == 0) {
            return Error(Error::Code::kParseError, "Failed to serialize embedded TLS public key");
        }
        std::vector<uint8_t> tls_pub_der(key_bytes, key_bytes + key_len);
        OPENSSL_free(key_bytes);

        int cert_len = i2d_X509(tls_cert.get(), nullptr);
        if (cert_len <= 0) {
            return Error(Error::Code::kParseError, "Failed to serialize embedded TLS certificate");
        }
        std::vector<uint8_t> tls_cert_der(static_cast<size_t>(cert_len));
        uint8_t* cert_out = tls_cert_der.data();
        i2d_X509(tls_cert.get(), &cert_out);

        DeviceCredentials device_creds;
        device_creds.certs.emplace_back(
            reinterpret_cast<const char*>(pplay::cast::creds::kAuthCrt),
            pplay::cast::creds::kAuthCrtLen);
        device_creds.certs.emplace_back(
            reinterpret_cast<const char*>(pplay::cast::creds::kIntermediateCrt),
            pplay::cast::creds::kIntermediateCrtLen);

        auto provider = std::make_unique<StaticCredentialsProvider>(
            std::move(device_creds), tls_cert_der);
        return GeneratedCredentials{
            std::move(provider),
            TlsCredentials{std::move(tls_key_der), std::move(tls_pub_der),
                           std::move(tls_cert_der)},
            std::vector<uint8_t>(pplay::cast::creds::kIntermediateCrt,
                                 pplay::cast::creds::kIntermediateCrt +
                                     pplay::cast::creds::kIntermediateCrtLen)};
    };

    ErrorOr<GeneratedCredentials> creds = buildHardcodedCredentials();
    if (!creds.is_value()) {
        log_info("Failed to build hardcoded credentials: " + creds.error().ToString());
        return;
    }
    auto *task_runner = new TaskRunnerImpl(&Clock::now);
    PlatformClientPosix::Create(milliseconds(50), std::unique_ptr<TaskRunnerImpl>(task_runner));
    std::unique_ptr<CastService> service;
    task_runner->PostTask([&] {
        service = std::make_unique<CastService>(CastService::Configuration{
            *task_runner,
            interface,
            std::move(creds.value()),
            deviceId,
            friendlyName,
            modelName,
            enableDiscovery,
            enableDscp,
        });
    });
    {
        std::lock_guard<std::mutex> lock(g_receiverRuntime.mutex);
        g_receiverRuntime.runner = task_runner;
        g_receiverRuntime.serviceCreated = true;
        g_receiverRuntime.stopRequested = false;
    }
    log_info("CastService is running on interface " + interfaceName);
    task_runner->RunUntilStopped();
    task_runner->PostTask([&] {
        service.reset();
        task_runner->RequestStopSoon();
    });
    task_runner->RunUntilStopped();
    PlatformClientPosix::ShutDown();
    {
        std::lock_guard<std::mutex> lock(g_receiverRuntime.mutex);
        g_receiverRuntime.runner = nullptr;
        g_receiverRuntime.serviceCreated = false;
    }
    log_info("CastService stopped");
}

void requestCastServiceStop() {
    std::lock_guard<std::mutex> lock(g_receiverRuntime.mutex);
    if (g_receiverRuntime.runner) {
        g_receiverRuntime.runner->RequestStopSoon();
    }
}

} // namespace

// ==================== MAIN CLASS ====================

PPLAYCast::PPLAYCast(Main *main) : main(main) {}

PPLAYCast::~PPLAYCast() {
    stop();
}

void PPLAYCast::start() {
    if (running || !main) return;

    if (main->getConfig()->getOption(OPT_CAST_ENABLED)->getInteger() == 0) {
       log_info("CastService disabled by config");
        return;
    }

    httpPort = 8008;
    castPort = 8010;

    running = true;
    log_info("Starting receiver='" + receiverName() + "' http=" + std::to_string(httpPort));

    httpThread = std::thread(&PPLAYCast::workerLoop, this);

    castThread = std::thread([this] {
        std::string interfaceName = pickInterfaceName();
        if (interfaceName.empty()) {
            ::log_info("No suitable network interface found");
            return;
        }

        std::string friendlyName = receiverName();
        std::string modelName = "pPlay Cast Receiver";
        std::string deviceId = chooseCredentialId(friendlyName, httpPort);
        bool enableDiscovery = true;
        bool enableDscp = true;

        try {
            ::runCastServiceOnThread(interfaceName, friendlyName, modelName, enableDiscovery, enableDscp, deviceId);
        } catch (...) {
            ::log_info("Cast service thread crashed");
        }
    });
}

void PPLAYCast::stop() {
    if (!running) return;
    running = false;
   log_info("stopping");
    requestCastServiceStop();

    if (httpThread.joinable()) httpThread.join();
    if (castThread.joinable()) castThread.join();
}

void PPLAYCast::reloadFromConfig() {
    bool shouldRun = main && main->getConfig()->getOption(OPT_CAST_ENABLED)->getInteger() != 0;
    if (shouldRun != running) {
        if (running) stop();
        else start();
    }
}

bool PPLAYCast::isRunning() const { return running; }

void PPLAYCast::enqueue(const Command &command) {
    std::lock_guard<std::mutex> lock(queueMutex);
    pendingCommands.push(command);
}

std::vector<PPLAYCast::Command> PPLAYCast::popCommands() {
    std::vector<Command> commands;
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!pendingCommands.empty()) {
        commands.push_back(pendingCommands.front());
        pendingCommands.pop();
    }
    return commands;
}

std::string PPLAYCast::receiverName() const {
    return chooseFriendlyName(main);
}

std::string PPLAYCast::uuid() const {
    std::string seed = receiverName() + ":" + std::to_string(httpPort);
    std::string hash = Utility::md5hash(seed);
    return hash.substr(0, 8) + "-" + hash.substr(8, 4) + "-" + hash.substr(12, 4) + "-"
           + hash.substr(16, 4) + "-" + hash.substr(20, 12);
}

std::string PPLAYCast::buildDeviceDescription() const {
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\"?>\n"
       << "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
       << "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
       << "  <device>\n"
       << "    <deviceType>urn:dial-multiscreen-org:device:dial:1</deviceType>\n"
       << "    <friendlyName>" << xmlEscape(receiverName()) << "</friendlyName>\n"
       << "    <manufacturer>pPlay</manufacturer>\n"
       << "    <modelName>pPlay Cast Receiver</modelName>\n"
       << "    <UDN>uuid:" << uuid() << "</UDN>\n"
       << "  </device>\n"
       << "</root>\n";
    return ss.str();
}

std::string PPLAYCast::buildStatusJson() const {
    auto *mpv = main->getPlayer()->getMpv();
    std::ostringstream ss;
    ss << "{\"name\":\"" << jsonEscape(receiverName()) << "\","
       << "\"running\":" << (running ? "true" : "false") << ","
       << "\"paused\":" << (mpv->isPaused() ? "true" : "false") << ","
       << "\"position\":" << mpv->getPosition() << ","
       << "\"duration\":" << mpv->getDuration() << ","
       << "\"path\":\"" << jsonEscape(mpv->getCurrentPath()) << "\"}";
    return ss.str();
}

std::string PPLAYCast::buildDialResponse(const std::string &appName) const {
    std::ostringstream ss;
    ss << "<service xmlns=\"urn:dial-multiscreen-org:schemas:dial\">\n"
       << "  <name>" << xmlEscape(appName) << "</name>\n"
       << "  <options allowStop=\"true\"/>\n"
       << "  <state>running</state>\n"
       << "</service>\n";
    return ss.str();
}

void PPLAYCast::workerLoop() {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
       log_info("http socket() failed");
        return;
    }

    setReuseAddr(server);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(httpPort));
    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(server, 8) != 0) {
       log_info("http bind/listen failed on port " + std::to_string(httpPort));
        closeSocket(server);
        return;
    }

    setNonBlocking(server);
   log_info("http listener ready");

    while (running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(server, &readSet);
        timeval tv{0, 200000};
        int ready = select(server + 1, &readSet, nullptr, nullptr, &tv);
        if (ready <= 0 || !FD_ISSET(server, &readSet)) continue;

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int client = accept(server, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
        if (client < 0) continue;

        char buffer[8192]{};
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        std::string response = notFoundResponse();
        if (received > 0) {
            std::string request(buffer, static_cast<size_t>(received));
            auto parts = splitRequestLine(request);
            std::string method = parts.size() > 0 ? parts[0] : "";
            std::string target = parts.size() > 1 ? parts[1] : "/";
           log_info("http " + method + " " + target);

            if (target == "/" || target == "/ssdp/device-desc.xml" || target == "/setup/eureka_info") {
                std::string host = headerValue(request, "Host");
                if (host.empty()) host = "127.0.0.1:" + std::to_string(httpPort);
                std::string extra = "Application-URL: http://" + host + "/apps/\r\n";
                response = httpResponse(buildDeviceDescription(), "application/xml; charset=utf-8", extra);
            } else if (target.rfind("/apps/", 0) == 0) {
                std::string app = target.substr(6);
                size_t q = app.find('?');
                if (q != std::string::npos) app = app.substr(0, q);
                response = httpResponse(buildDialResponse(app.empty() ? "DefaultMediaReceiver" : app),
                                        "application/xml; charset=utf-8");
            } else if (target.rfind("/pplay/status", 0) == 0) {
                response = httpResponse(buildStatusJson(), "application/json; charset=utf-8");
            } else if (target.rfind("/pplay/control", 0) == 0) {
                std::string command = lower(queryParam(target, "command"));
                if (command == "play") enqueue({CommandType::Play});
                else if (command == "pause") enqueue({CommandType::Pause});
                else if (command == "toggle" || command == "playpause") enqueue({CommandType::TogglePause});
                else if (command == "stop") enqueue({CommandType::Stop});
                else if (command == "seek") enqueue({CommandType::SeekRelative, std::atof(queryParam(target, "seconds").c_str())});
                else if (command == "volume") enqueue({CommandType::VolumeRelative, std::atof(queryParam(target, "delta").c_str())});
                response = okTextResponse();
            } else if (target.rfind("/pplay/load", 0) == 0) {
                std::string url = queryParam(target, "url");
                if (!url.empty()) enqueue({CommandType::LoadUrl, 0.0, url});
                response = okTextResponse(url.empty() ? "Missing url" : "Loading");
            }
        }

        send(client, response.c_str(), response.size(), 0);
        closeSocket(client);
    }

    closeSocket(server);
}
