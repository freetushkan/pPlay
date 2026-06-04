#include "cast_service.h"

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
        if (out_len) *out_len = 0;
        if (out_bytes) *out_bytes = nullptr;
        return 0;
    }
    int RSA_public_key_to_bytes(uint8_t **out_bytes, size_t *out_len, const RSA *rsa) {
        if (out_len) *out_len = 0;
        if (out_bytes) *out_bytes = nullptr;
        return 0;
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


static unsigned char auth_crt[] = {
  0x30, 0x82, 0x03, 0xab, 0x30, 0x82, 0x02, 0x93, 0xa0, 0x03, 0x02, 0x01,
  0x02, 0x02, 0x04, 0x52, 0x5d, 0xb4, 0xc8, 0x30, 0x0d, 0x06, 0x09, 0x2a,
  0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x7d,
  0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55,
  0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x0c, 0x0a,
  0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x16,
  0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, 0x0d, 0x4d, 0x6f, 0x75,
  0x6e, 0x74, 0x61, 0x69, 0x6e, 0x20, 0x56, 0x69, 0x65, 0x77, 0x31, 0x13,
  0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0a, 0x47, 0x6f, 0x6f,
  0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x31, 0x12, 0x30, 0x10, 0x06,
  0x03, 0x55, 0x04, 0x0b, 0x0c, 0x09, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65,
  0x20, 0x54, 0x56, 0x31, 0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04, 0x03,
  0x0c, 0x0f, 0x45, 0x75, 0x72, 0x65, 0x6b, 0x61, 0x20, 0x47, 0x65, 0x6e,
  0x31, 0x20, 0x49, 0x43, 0x41, 0x30, 0x1e, 0x17, 0x0d, 0x31, 0x33, 0x31,
  0x30, 0x31, 0x35, 0x32, 0x31, 0x33, 0x34, 0x30, 0x30, 0x5a, 0x17, 0x0d,
  0x33, 0x33, 0x31, 0x30, 0x31, 0x30, 0x32, 0x31, 0x33, 0x34, 0x30, 0x30,
  0x5a, 0x30, 0x81, 0x80, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04,
  0x0a, 0x13, 0x0a, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e,
  0x63, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0a,
  0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x0b,
  0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31,
  0x16, 0x30, 0x14, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x0d, 0x4d, 0x6f,
  0x75, 0x6e, 0x74, 0x61, 0x69, 0x6e, 0x20, 0x56, 0x69, 0x65, 0x77, 0x31,
  0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x09, 0x47, 0x6f,
  0x6f, 0x67, 0x6c, 0x65, 0x20, 0x54, 0x56, 0x31, 0x1b, 0x30, 0x19, 0x06,
  0x03, 0x55, 0x04, 0x03, 0x13, 0x12, 0x57, 0x44, 0x46, 0x54, 0x33, 0x20,
  0x46, 0x41, 0x38, 0x46, 0x43, 0x41, 0x38, 0x39, 0x35, 0x44, 0x35, 0x39,
  0x30, 0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
  0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00,
  0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00, 0xca, 0xe2, 0x5e,
  0x88, 0xbd, 0xe4, 0xbb, 0xe9, 0x93, 0x2b, 0x61, 0x30, 0x39, 0xd6, 0xea,
  0x7f, 0x40, 0x0f, 0x11, 0xea, 0xaa, 0x6b, 0x41, 0xce, 0x4c, 0x0d, 0x15,
  0x09, 0x74, 0x17, 0x18, 0xe2, 0x5d, 0x11, 0x76, 0xe8, 0x0a, 0x73, 0x64,
  0x49, 0xbe, 0x3a, 0x03, 0x40, 0xa7, 0xd4, 0xe3, 0xbb, 0xff, 0x64, 0x7b,
  0x13, 0x1d, 0x0e, 0x96, 0x14, 0xa8, 0x62, 0xff, 0xf6, 0xc4, 0xa8, 0x1b,
  0x3a, 0x92, 0xd2, 0xa7, 0x78, 0x2a, 0x5e, 0x5d, 0x5a, 0x44, 0xd3, 0x0d,
  0x0f, 0x58, 0xbe, 0x61, 0x57, 0x43, 0xcd, 0xd4, 0xba, 0x17, 0xd2, 0x16,
  0xbf, 0x1d, 0x62, 0x4d, 0x84, 0xae, 0x3e, 0xcc, 0x54, 0x48, 0x7e, 0x44,
  0x0b, 0x31, 0x25, 0xe6, 0x5b, 0xc3, 0x49, 0x65, 0xcf, 0x22, 0x7e, 0x78,
  0x02, 0x69, 0x15, 0x1b, 0x4e, 0x31, 0xb9, 0x51, 0x13, 0x7c, 0xb2, 0xd0,
  0xcb, 0xc7, 0x5b, 0x66, 0xc8, 0xe1, 0xf3, 0xd3, 0x83, 0x3d, 0x30, 0xd1,
  0x2c, 0x33, 0x83, 0x7f, 0xbf, 0x25, 0x7c, 0xa6, 0xa0, 0xbc, 0x79, 0x85,
  0x9d, 0x63, 0xd7, 0x05, 0xd3, 0xcf, 0xa1, 0x46, 0x28, 0x92, 0x62, 0xc7,
  0xdc, 0x8b, 0xb1, 0xa7, 0xbc, 0xe5, 0xf4, 0x6e, 0xe8, 0xc4, 0xbf, 0xfe,
  0x3f, 0xa5, 0x4f, 0x92, 0x0c, 0xa5, 0x03, 0x31, 0xaa, 0x9d, 0x2b, 0xe6,
  0xb4, 0xc7, 0x43, 0xb3, 0xa2, 0xf3, 0xfc, 0x66, 0x76, 0x29, 0x7e, 0x56,
  0x30, 0xa0, 0x7c, 0x9f, 0xe7, 0x36, 0x83, 0x55, 0x0c, 0x8c, 0xf8, 0x1a,
  0x8a, 0xeb, 0x7a, 0xef, 0x07, 0xff, 0x57, 0xa9, 0x6d, 0xbd, 0x05, 0xcd,
  0xf7, 0x9b, 0xae, 0x7b, 0xd2, 0x62, 0x1c, 0x11, 0x02, 0x81, 0x95, 0x9b,
  0x61, 0x36, 0x9d, 0x23, 0xd6, 0x88, 0xa5, 0xd6, 0xc4, 0xc7, 0x06, 0xc2,
  0x76, 0x97, 0x91, 0x16, 0xcf, 0x2f, 0xb3, 0x1e, 0x48, 0x4e, 0x61, 0x65,
  0x2f, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x2f, 0x30, 0x2d, 0x30, 0x09,
  0x06, 0x03, 0x55, 0x1d, 0x13, 0x04, 0x02, 0x30, 0x00, 0x30, 0x0b, 0x06,
  0x03, 0x55, 0x1d, 0x0f, 0x04, 0x04, 0x03, 0x02, 0x07, 0x80, 0x30, 0x13,
  0x06, 0x03, 0x55, 0x1d, 0x25, 0x04, 0x0c, 0x30, 0x0a, 0x06, 0x08, 0x2b,
  0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02, 0x30, 0x0d, 0x06, 0x09, 0x2a,
  0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x82,
  0x01, 0x01, 0x00, 0x73, 0xf4, 0xf5, 0x85, 0x0d, 0x35, 0x92, 0x39, 0x04,
  0x4e, 0x0d, 0xa5, 0x2d, 0x73, 0xc8, 0x71, 0x81, 0xb7, 0x9c, 0xfe, 0x51,
  0x5c, 0x8c, 0x83, 0xc2, 0x75, 0xe5, 0x35, 0x6b, 0x07, 0x57, 0x36, 0x65,
  0xcf, 0xf5, 0x6f, 0xaa, 0xaf, 0xeb, 0x2e, 0x64, 0x30, 0x26, 0x37, 0x50,
  0x8d, 0x55, 0xa3, 0x3f, 0xe9, 0x99, 0x97, 0x0e, 0x98, 0xdc, 0xef, 0x2a,
  0xd1, 0xc5, 0x4a, 0x50, 0x45, 0x95, 0xb6, 0x4e, 0x06, 0x1a, 0x4e, 0x96,
  0x1d, 0xf0, 0x8c, 0xfa, 0xcf, 0xe3, 0x7b, 0x39, 0xda, 0x78, 0x41, 0xa0,
  0xdc, 0xb2, 0x36, 0x4e, 0xe5, 0x02, 0xc8, 0x3d, 0x91, 0x91, 0xb6, 0x90,
  0xa3, 0x34, 0x90, 0x1a, 0x3b, 0xd4, 0xd4, 0xd5, 0xf2, 0x23, 0xf7, 0xd8,
  0x34, 0x1a, 0x1c, 0x88, 0x17, 0xe4, 0x7f, 0x9f, 0x78, 0xa6, 0x74, 0x52,
  0xfe, 0xc5, 0xce, 0x52, 0x2b, 0x2e, 0xa3, 0xc2, 0x32, 0xbb, 0x03, 0x7f,
  0xa3, 0x70, 0x9e, 0x89, 0x7f, 0x85, 0x02, 0x48, 0xb3, 0x62, 0x25, 0xe1,
  0xad, 0x46, 0x02, 0xbc, 0xf4, 0x29, 0x16, 0xd3, 0xa0, 0x1d, 0x06, 0xd2,
  0xb8, 0xd7, 0x83, 0x13, 0x64, 0x7f, 0x1c, 0xac, 0x8e, 0x2a, 0x45, 0xbf,
  0x0e, 0x50, 0xd1, 0x58, 0xef, 0x6d, 0x41, 0xaf, 0x8e, 0xf5, 0x22, 0xe2,
  0xfd, 0x4a, 0x82, 0xb1, 0xa0, 0x31, 0x3a, 0xa9, 0x4e, 0x83, 0x7d, 0x8f,
  0x5b, 0x5a, 0xed, 0xd5, 0xf4, 0x79, 0x0a, 0x04, 0xd3, 0xe4, 0x0d, 0xe4,
  0x7c, 0x4c, 0x57, 0xa0, 0x0e, 0x27, 0xd0, 0xb2, 0x7e, 0x08, 0x41, 0xfc,
  0x51, 0xe9, 0xdd, 0x74, 0x4b, 0xa1, 0xe4, 0xa6, 0x7e, 0xc6, 0xe5, 0x0f,
  0x5a, 0x4c, 0x4f, 0x41, 0x33, 0x84, 0x00, 0x4d, 0x0c, 0xb1, 0xcc, 0x5d,
  0xb6, 0xfa, 0xed, 0x3b, 0x16, 0xfd, 0x66, 0x94, 0xdf, 0x51, 0xa8, 0x5d,
  0x9d, 0x37, 0xae, 0xd8, 0x4e, 0x0f, 0x91
};

unsigned int auth_crt_len = 943;

static unsigned char peer_key_der[] = {
  0x30, 0x82, 0x04, 0xa4, 0x02, 0x01, 0x00, 0x02, 0x82, 0x01, 0x01, 0x00,
  0xc2, 0x6c, 0x3e, 0x77, 0xcd, 0xb4, 0xf8, 0x10, 0xd6, 0xd3, 0x34, 0x08,
  0xd5, 0x3e, 0x18, 0xa2, 0xdd, 0xaf, 0xac, 0x03, 0x42, 0xb5, 0xe2, 0x0b,
  0x70, 0x35, 0x23, 0x64, 0xde, 0xf6, 0x4c, 0xb6, 0x46, 0x44, 0x7f, 0x2b,
  0xa9, 0x46, 0xa9, 0x71, 0x09, 0x4f, 0x42, 0xa5, 0x17, 0xac, 0xaf, 0xe7,
  0x26, 0x37, 0x87, 0xc7, 0xd8, 0x33, 0xf3, 0x0d, 0x71, 0x65, 0x7a, 0x93,
  0xec, 0x29, 0x69, 0x00, 0xc8, 0x0a, 0x03, 0x0c, 0x9d, 0xde, 0x22, 0xda,
  0xa9, 0xdd, 0x49, 0x61, 0xbe, 0x2b, 0xcc, 0xfb, 0x0d, 0x1a, 0xf5, 0xc8,
  0x2a, 0x5e, 0x41, 0xdb, 0x56, 0x82, 0x56, 0x2d, 0x39, 0x84, 0x87, 0x3c,
  0x06, 0x8e, 0xcd, 0xab, 0x69, 0x7c, 0xac, 0x09, 0x9a, 0x4f, 0x0d, 0x46,
  0xe3, 0x72, 0xb7, 0xe7, 0x94, 0x8d, 0x7b, 0xb6, 0xbe, 0x64, 0x65, 0x3d,
  0x0a, 0xe8, 0xce, 0x8c, 0x06, 0x18, 0xb6, 0xf8, 0x59, 0xfa, 0xe6, 0x74,
  0xe9, 0x7f, 0xe1, 0xe3, 0x81, 0xde, 0x26, 0x1f, 0xd8, 0xa9, 0x34, 0x92,
  0x05, 0x77, 0x0c, 0xae, 0x28, 0xa2, 0x9e, 0xf1, 0x2d, 0x5d, 0x52, 0x8f,
  0xfd, 0xc2, 0x6b, 0x6d, 0xfd, 0xe8, 0x68, 0x2f, 0xf4, 0x64, 0x95, 0x82,
  0xbb, 0x89, 0x2e, 0x2e, 0xca, 0x26, 0x24, 0x4a, 0xd7, 0xfa, 0x03, 0xcf,
  0x0d, 0x52, 0xd6, 0x23, 0x46, 0xa2, 0xe4, 0x9f, 0x86, 0x96, 0xbc, 0x32,
  0xcd, 0x44, 0xc4, 0x63, 0x2a, 0xc7, 0xfb, 0x3f, 0xe0, 0x28, 0x52, 0xd1,
  0xba, 0x54, 0x0a, 0x85, 0xe7, 0x09, 0x5d, 0x0c, 0x36, 0x93, 0xa6, 0xea,
  0xce, 0x6f, 0x65, 0x08, 0x36, 0x13, 0x4b, 0x0b, 0x65, 0xc9, 0xce, 0xee,
  0x15, 0x2e, 0x83, 0xee, 0x4f, 0x16, 0xdb, 0xc8, 0x28, 0x68, 0xdb, 0x39,
  0xe1, 0x47, 0x96, 0x53, 0x57, 0x7b, 0x02, 0x05, 0xf1, 0xde, 0xcf, 0x74,
  0xf9, 0x4c, 0x41, 0xcf, 0x02, 0x03, 0x01, 0x00, 0x01, 0x02, 0x82, 0x01,
  0x00, 0x41, 0x5f, 0x4c, 0x11, 0xd4, 0x65, 0x09, 0x14, 0x00, 0x67, 0xbb,
  0x93, 0x4c, 0xc0, 0x38, 0x60, 0x6a, 0xd1, 0xea, 0xb0, 0x9d, 0xf9, 0xb2,
  0x2b, 0xce, 0x6a, 0xcf, 0x9f, 0xd7, 0x28, 0x51, 0xda, 0xe7, 0xfd, 0x98,
  0x15, 0x02, 0x31, 0xf4, 0x3f, 0x41, 0xb6, 0x18, 0xde, 0x91, 0xfb, 0x4a,
  0x9a, 0x1a, 0x4b, 0x89, 0xa8, 0x34, 0x96, 0x23, 0x1f, 0x5e, 0x05, 0x95,
  0x15, 0xaf, 0xce, 0xac, 0xb3, 0xca, 0x8f, 0x33, 0x3f, 0x46, 0xc5, 0xae,
  0x4a, 0x7f, 0xdb, 0x1c, 0x15, 0x75, 0x70, 0x1c, 0xd8, 0x3d, 0x2b, 0xd7,
  0x80, 0x9a, 0x5e, 0x5f, 0x1e, 0x75, 0x14, 0x16, 0x0d, 0xd6, 0xcd, 0x2c,
  0xfb, 0x8d, 0xe8, 0xee, 0x56, 0xb8, 0x7d, 0x67, 0x0b, 0x43, 0x8b, 0x59,
  0x17, 0x80, 0xda, 0xcd, 0xe3, 0x5c, 0x1b, 0xc6, 0x81, 0x47, 0xbb, 0x52,
  0x1f, 0x18, 0x4f, 0xf7, 0x43, 0x35, 0xb9, 0x91, 0xab, 0x91, 0x4c, 0xfd,
  0x93, 0x6a, 0xb1, 0x3f, 0x9a, 0xeb, 0x94, 0x5d, 0xa9, 0x79, 0xa2, 0x8a,
  0x69, 0x1a, 0x94, 0xb1, 0xd3, 0xef, 0x94, 0x55, 0x86, 0x63, 0xbf, 0xb8,
  0x6f, 0x49, 0xdf, 0x62, 0x18, 0xfe, 0x80, 0xe5, 0x53, 0xb1, 0x30, 0xe8,
  0x90, 0x48, 0x45, 0xbe, 0xb3, 0xb8, 0xdf, 0xc1, 0x84, 0x1b, 0x4f, 0x10,
  0x47, 0x90, 0x30, 0x79, 0x0d, 0x2d, 0xa9, 0x5c, 0xe6, 0xec, 0xa4, 0xd4,
  0x5b, 0x92, 0x21, 0x82, 0x7f, 0xab, 0x6f, 0x6c, 0x01, 0xd0, 0x6a, 0x11,
  0x4a, 0x5e, 0x27, 0xb2, 0x1c, 0x3a, 0x76, 0x46, 0x5f, 0x20, 0x7d, 0x4d,
  0xf1, 0x33, 0x72, 0xc1, 0x84, 0xd5, 0xd2, 0x5f, 0x56, 0x55, 0xf5, 0xcf,
  0x15, 0x2d, 0xdc, 0x67, 0x1a, 0xb5, 0x1b, 0xf9, 0xaa, 0x44, 0x3e, 0x26,
  0xd4, 0x6f, 0xfc, 0xf7, 0x51, 0x3f, 0x92, 0xbe, 0x8e, 0x3f, 0x25, 0x01,
  0x7f, 0x40, 0xd0, 0x95, 0xe1, 0x02, 0x81, 0x81, 0x00, 0xf4, 0x7f, 0x23,
  0x8c, 0xae, 0x91, 0xfc, 0x64, 0x4f, 0x70, 0xbe, 0x0c, 0x2a, 0x28, 0xf4,
  0xfe, 0x08, 0x4d, 0x62, 0xde, 0x4a, 0x09, 0x77, 0x0e, 0x25, 0xf1, 0xad,
  0xfa, 0x04, 0xea, 0xed, 0x5d, 0x03, 0x3b, 0x6d, 0x2c, 0x13, 0x77, 0x2d,
  0x5f, 0xed, 0x7f, 0xb1, 0x7b, 0xc6, 0x1d, 0x4f, 0xb0, 0x81, 0x2b, 0x2b,
  0xaa, 0x66, 0xc6, 0xc1, 0x19, 0xbd, 0x29, 0xf4, 0x59, 0x21, 0x61, 0x41,
  0x82, 0x35, 0xb9, 0x25, 0xb1, 0xcb, 0xa6, 0xf9, 0xcc, 0x83, 0x05, 0x77,
  0xa9, 0x50, 0x86, 0x78, 0x7d, 0x25, 0x3b, 0x17, 0x2e, 0x1b, 0x26, 0x31,
  0x12, 0x3c, 0xb8, 0xa9, 0xe2, 0x58, 0x89, 0x2d, 0x63, 0xe6, 0xcd, 0x40,
  0x8a, 0x6c, 0xf1, 0x92, 0x52, 0x60, 0x6b, 0xce, 0x5a, 0x7e, 0xf3, 0xae,
  0xf8, 0x98, 0x04, 0xae, 0xe6, 0xb1, 0x97, 0x24, 0xb0, 0xd2, 0x45, 0x11,
  0x77, 0x21, 0xe6, 0xbf, 0xfd, 0x02, 0x81, 0x81, 0x00, 0xcb, 0x91, 0xfc,
  0x9e, 0x0f, 0xd8, 0x2b, 0x27, 0x0d, 0xd1, 0x59, 0x0b, 0xbe, 0x58, 0xcb,
  0x98, 0x34, 0xc4, 0x07, 0xac, 0x41, 0xf2, 0x5d, 0x3f, 0xa0, 0xb2, 0xd4,
  0xf8, 0xd5, 0x07, 0x7e, 0xe5, 0xfc, 0xee, 0x45, 0xa3, 0x60, 0xfd, 0x76,
  0xfe, 0x09, 0xae, 0x23, 0xe1, 0x12, 0x66, 0x4c, 0x96, 0xa8, 0x8e, 0xac,
  0xc1, 0xb6, 0x34, 0xfb, 0xec, 0xb1, 0xc7, 0x41, 0x97, 0xd6, 0x98, 0xdd,
  0x9e, 0x58, 0xd8, 0x79, 0x15, 0xdc, 0xca, 0xf3, 0x67, 0x0b, 0x65, 0xd8,
  0x5d, 0x59, 0x30, 0x80, 0x30, 0xda, 0x7f, 0x97, 0x2c, 0x3c, 0x46, 0xc5,
  0x00, 0x31, 0x02, 0xed, 0xfd, 0x7d, 0xba, 0x91, 0xe9, 0x9b, 0x60, 0xaf,
  0x88, 0xa2, 0xc8, 0x63, 0x24, 0xeb, 0xf1, 0x18, 0x99, 0xf8, 0xba, 0xcb,
  0x79, 0xc2, 0x9f, 0x86, 0x8e, 0x71, 0xb4, 0x7c, 0xcd, 0xd4, 0xcb, 0x21,
  0xb6, 0x40, 0x6b, 0x54, 0xbb, 0x02, 0x81, 0x80, 0x1f, 0x64, 0xbf, 0xcc,
  0xcd, 0x91, 0x83, 0x25, 0xe2, 0x29, 0x68, 0xcd, 0xa9, 0x10, 0x2f, 0x3c,
  0xfb, 0x15, 0xec, 0xae, 0xfc, 0x34, 0xb0, 0xeb, 0xc9, 0x25, 0x7a, 0x20,
  0x53, 0x47, 0x53, 0x09, 0x11, 0x64, 0x2d, 0x05, 0x6e, 0xce, 0x6b, 0xae,
  0x18, 0x91, 0xbf, 0xd9, 0x53, 0xbb, 0xe9, 0xc2, 0x91, 0x23, 0x58, 0xec,
  0xfd, 0x5b, 0x61, 0xea, 0x0b, 0x26, 0xfa, 0xf0, 0x02, 0xe0, 0x39, 0x08,
  0x1e, 0x1b, 0xd2, 0xe2, 0x3c, 0x73, 0x09, 0x3a, 0x20, 0x4c, 0xb0, 0x6b,
  0xb6, 0x22, 0x3e, 0x10, 0x5b, 0x9a, 0x75, 0xc4, 0x7e, 0xc9, 0xed, 0x9d,
  0x18, 0xdc, 0xe1, 0x3b, 0x66, 0x00, 0xdf, 0x2a, 0x27, 0xb5, 0x8d, 0x26,
  0xf9, 0x0c, 0x1f, 0xac, 0xa2, 0x22, 0xd5, 0x91, 0x3b, 0x21, 0xd1, 0x02,
  0xac, 0x8a, 0x55, 0x66, 0xdb, 0xc0, 0xbb, 0x7e, 0x54, 0xd1, 0x77, 0x87,
  0xa3, 0x7b, 0xbc, 0x85, 0x02, 0x81, 0x80, 0x20, 0x1e, 0x34, 0x61, 0x2b,
  0xd6, 0xcb, 0x58, 0x2a, 0x11, 0xf1, 0x9e, 0xac, 0xb5, 0x8d, 0xc9, 0xc1,
  0xe9, 0x7b, 0xdc, 0x6d, 0xbb, 0x33, 0x83, 0x2a, 0x73, 0x8c, 0xae, 0x85,
  0xcd, 0xdc, 0xf3, 0xa4, 0x68, 0x63, 0x8c, 0x57, 0x6d, 0x26, 0x2d, 0x06,
  0x91, 0xf7, 0x0f, 0x37, 0xbb, 0xf4, 0x31, 0x80, 0xfe, 0xa1, 0xbb, 0x1e,
  0x68, 0x55, 0xa7, 0x0e, 0x95, 0x85, 0x7f, 0xd3, 0x57, 0xe2, 0xff, 0x6c,
  0xbd, 0x1a, 0xbd, 0x9c, 0x4c, 0x59, 0x02, 0xd5, 0x05, 0x88, 0x91, 0x91,
  0xd4, 0xd9, 0x24, 0xdc, 0x14, 0x6d, 0x61, 0x89, 0x51, 0x11, 0x1b, 0xea,
  0x9f, 0xaf, 0xb4, 0xe2, 0xf5, 0x60, 0xb8, 0x1e, 0xcf, 0xae, 0x62, 0x3c,
  0x6c, 0xa9, 0x57, 0xd5, 0xf2, 0x00, 0x13, 0xaa, 0xee, 0xad, 0xd3, 0xd4,
  0x25, 0x1f, 0x31, 0xb2, 0x36, 0xeb, 0xc9, 0xfd, 0xdc, 0xde, 0xc0, 0xc6,
  0x81, 0x28, 0xa9, 0x02, 0x81, 0x80, 0x32, 0x5e, 0xb9, 0x6c, 0xc4, 0xc7,
  0x84, 0xd1, 0x27, 0x72, 0x86, 0xbd, 0xfa, 0x7f, 0x42, 0x5b, 0xba, 0xd0,
  0x05, 0x07, 0x34, 0xbb, 0x3c, 0x06, 0xdd, 0x0f, 0xf0, 0xbe, 0x81, 0xb6,
  0xde, 0x3f, 0xfe, 0x96, 0x9f, 0x08, 0xae, 0xf2, 0x2a, 0xcd, 0x5a, 0xef,
  0xa3, 0xc7, 0xe6, 0x68, 0x6c, 0xa6, 0x52, 0xc4, 0xfd, 0xb2, 0x2a, 0xbf,
  0x9a, 0x34, 0xde, 0xeb, 0x40, 0x26, 0x88, 0x5c, 0x28, 0x62, 0x25, 0xd5,
  0x60, 0x4d, 0x7f, 0x57, 0x9f, 0x28, 0x35, 0x03, 0xe0, 0xd7, 0x59, 0x25,
  0x00, 0x00, 0xac, 0xf4, 0xc4, 0xf5, 0xa9, 0x36, 0xb9, 0x3f, 0xff, 0xd7,
  0x11, 0x0a, 0x80, 0x75, 0xb9, 0x4a, 0x18, 0x5d, 0xbf, 0x44, 0x3a, 0x9a,
  0x0c, 0x0c, 0x58, 0x50, 0x85, 0xf0, 0x53, 0xf5, 0x49, 0xd8, 0x26, 0x71,
  0x2d, 0x7f, 0x67, 0x39, 0x09, 0xbd, 0x0f, 0x05, 0x25, 0x8d, 0xf6, 0x38,
  0xfa, 0xb4
};

unsigned int peer_key_der_len = 1190;

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
        std::unique_ptr<RSA, decltype(&RSA_free)> rsa(
            RSA_private_key_from_bytes(peer_key_der, peer_key_der_len), &RSA_free);
        if (!rsa) {
            return Error(Error::Code::kParseError, "Failed to parse embedded private key");
        }

        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(
            EVP_PKEY_new(), &EVP_PKEY_free);
        if (!private_key) {
            return Error(Error::Code::kParseError, "Failed to allocate EVP_PKEY");
        }
        if (EVP_PKEY_assign_RSA(private_key.get(), rsa.get()) != 1) {
            return Error(Error::Code::kParseError, "Failed to attach embedded private key");
        }
        const unsigned char* cert_ptr = auth_crt;
        std::unique_ptr<X509, decltype(&X509_free)> certificate(
            d2i_X509(nullptr, &cert_ptr, auth_crt_len), &X509_free);
        if (!certificate) {
            return Error(Error::Code::kParseError, "Failed to parse embedded certificate");
        }
        // return openscreen::cast::GenerateCredentials(deviceId, private_key.get(), certificate.get());
        GeneratedCredentials creds;
        creds.private_key.reset(private_key.release());
        creds.cert.reset(certificate.release());
        creds.device_id = deviceId;
        return creds;
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
