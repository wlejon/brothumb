// Canonical URI conversion and hash functions per the Freedesktop Thumbnail Spec.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace brothumb {

// Converts a local filesystem path into a canonical Freedesktop file:// URI.
// Normalizes path segments (., ..), collapses duplicate slashes, and
// percent-encodes characters according to RFC 3986 / Freedesktop standard.
std::string path_to_uri(const std::filesystem::path& path);

// Converts a canonical file:// URI back to a local filesystem path.
// Decodes percent-encoded bytes and handles Windows drive letters or POSIX roots.
std::filesystem::path uri_to_path(std::string_view uri);

// Computes the lowercase 32-character hexadecimal MD5 digest of the input.
std::string md5_hex(std::string_view data);

// Computes the lowercase 64-character hexadecimal SHA-256 digest of the input.
std::string sha256_hex(std::string_view data);

// Computes the standard thumbnail filename (e.g. "6756f54a791d53a4ece8ebb70471b573.png")
// for a canonical URI using MD5.
std::string uri_to_thumbnail_filename(std::string_view canonical_uri);

// Computes the standard thumbnail filename using SHA-256 (modern Freedesktop extension).
std::string uri_to_thumbnail_filename_sha256(std::string_view canonical_uri);

}  // namespace brothumb
