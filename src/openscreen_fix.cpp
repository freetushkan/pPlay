#include <string>
#include <vector>
#include <memory>

namespace openscreen {
    class Error {
    public:
        enum class Code { kUnknownError = 1 };
        Error(Code code) {}
    };

    template <typename T>
    class ErrorOr {
        int placeholder;
    public:
        ErrorOr(Error error) : placeholder(0) {}
    };
}

namespace openscreen::cast {
    class StaticCredentialsProvider;
    struct TlsCredentials {};
    struct GeneratedCredentials {
        std::unique_ptr<StaticCredentialsProvider> provider;
        TlsCredentials tls_credentials;
        std::vector<uint8_t> root_cert_der;
    };
    openscreen::ErrorOr<GeneratedCredentials> GenerateCredentials(
        const std::string& device_certificate_id,
        const std::string& private_key_path,
        const std::string& server_certificate_path) 
    {
        return openscreen::ErrorOr<GeneratedCredentials>(
            openscreen::Error(openscreen::Error::Code::kUnknownError)
        );
    }
}
