#include "brothumb/uri.h"
#include "md5.h"
#include "sha256.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace brothumb {

namespace {

// Checks whether character is unescaped in Freedesktop canonical URI path.
// Matches GLib's g_filename_to_uri behavior.
inline bool is_uri_path_char(uint8_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
        case '-': case '_': case '.': case '~':
        case '!': case '$': case '&': case '\'':
        case '(': case ')': case '*': case '+':
        case ',': case '/': case ':': case '=':
        case '@':
            return true;
        default:
            return false;
    }
}

inline uint8_t hex_val(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0;
}

inline bool is_hex(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

}  // namespace

std::string path_to_uri(const std::filesystem::path& path) {
    std::filesystem::path abs_path = path;
    if (!abs_path.is_absolute()) {
        try {
            abs_path = std::filesystem::absolute(abs_path);
        } catch (...) {
        }
    }
    abs_path = abs_path.lexically_normal();

    auto u8_str = abs_path.generic_u8string();
    std::string generic_str(reinterpret_cast<const char*>(u8_str.data()), u8_str.size());

    std::string normalized;
    normalized.reserve(generic_str.size());

    // Collapse redundant slashes
    bool prev_was_slash = false;
    for (char c : generic_str) {
        if (c == '\\') c = '/';
        if (c == '/') {
            if (!prev_was_slash) {
                normalized.push_back('/');
                prev_was_slash = true;
            }
        } else {
            normalized.push_back(c);
            prev_was_slash = false;
        }
    }

    // Windows drive letter check: e.g. "C:/..." -> needs leading slash "/C:/..."
    std::string uri_path;
    if (normalized.size() >= 2 &&
        std::isalpha(static_cast<unsigned char>(normalized[0])) &&
        normalized[1] == ':') {
        uri_path = "/" + normalized;
    } else if (normalized.empty() || normalized[0] != '/') {
        uri_path = "/" + normalized;
    } else {
        uri_path = normalized;
    }

    // Percent-encode characters
    std::string result = "file://";
    static const char hex_chars[] = "0123456789ABCDEF";

    for (unsigned char uc : uri_path) {
        if (is_uri_path_char(uc)) {
            result.push_back(static_cast<char>(uc));
        } else {
            result.push_back('%');
            result.push_back(hex_chars[(uc >> 4) & 0x0F]);
            result.push_back(hex_chars[uc & 0x0F]);
        }
    }

    return result;
}

std::filesystem::path uri_to_path(std::string_view uri) {
    std::string_view prefix = "file://";
    if (uri.rfind(prefix, 0) == 0) {
        uri.remove_prefix(prefix.size());
    }

    // If starts with "localhost/", strip it
    std::string_view localhost = "localhost/";
    if (uri.rfind(localhost, 0) == 0) {
        uri.remove_prefix(localhost.size() - 1); // keep leading slash
    }

    // Decode percent-encoded octets
    std::string decoded;
    decoded.reserve(uri.size());
    for (size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size() && is_hex(uri[i + 1]) && is_hex(uri[i + 2])) {
            uint8_t byte = (hex_val(uri[i + 1]) << 4) | hex_val(uri[i + 2]);
            decoded.push_back(static_cast<char>(byte));
            i += 2;
        } else {
            decoded.push_back(uri[i]);
        }
    }

    // Windows drive letter: "/C:/..." -> "C:/..."
    if (decoded.size() >= 3 && decoded[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(decoded[1])) &&
        decoded[2] == ':') {
        decoded.erase(0, 1);
    }

    return std::filesystem::path(reinterpret_cast<const char8_t*>(decoded.data()),
                                 reinterpret_cast<const char8_t*>(decoded.data() + decoded.size()));
}

std::string md5_hex(std::string_view data) {
    return detail::MD5::hash_string(data);
}

std::string sha256_hex(std::string_view data) {
    return detail::SHA256::hash_string(data);
}

std::string uri_to_thumbnail_filename(std::string_view canonical_uri) {
    return md5_hex(canonical_uri) + ".png";
}

std::string uri_to_thumbnail_filename_sha256(std::string_view canonical_uri) {
    return sha256_hex(canonical_uri) + ".png";
}

}  // namespace brothumb
