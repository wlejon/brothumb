#include "brothumb/uri.h"
#include "check.h"

int main() {
    // 1. MD5 test vectors (RFC 1321)
    CHECK_EQ(brothumb::md5_hex(""), "d41d8cd98f00b204e9800998ecf8427e");
    CHECK_EQ(brothumb::md5_hex("a"), "0cc175b9c0f1b6a831c399e269772661");
    CHECK_EQ(brothumb::md5_hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
    CHECK_EQ(brothumb::md5_hex("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK_EQ(brothumb::md5_hex("The quick brown fox jumps over the lazy dog"),
             "9e107d9d372bb6826bd81d3542a419d6");

    // 2. SHA-256 test vectors (FIPS 180-2)
    CHECK_EQ(brothumb::sha256_hex(""),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(brothumb::sha256_hex("abc"),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 3. Freedesktop URI MD5 test vector
    // Verified against GLib g_filename_to_uri + md5sum on Linux
    CHECK_EQ(brothumb::uri_to_thumbnail_filename("file:///tmp/test.png"),
             "6756f54a791d53a4ece8ebb70471b573.png");

    // 4. URI Path encoding tests
    // Space -> %20
    // Hash -> %23
    // Question mark -> %3F
    // Square brackets -> %5B %5D
#if !defined(_WIN32)
    std::filesystem::path p1("/home/user/my file.png");
    CHECK_EQ(brothumb::path_to_uri(p1), "file:///home/user/my%20file.png");

    std::filesystem::path p2("/home/user/code #1.cpp");
    CHECK_EQ(brothumb::path_to_uri(p2), "file:///home/user/code%20%231.cpp");

    std::filesystem::path p3("/test-._~+!*().txt");
    CHECK_EQ(brothumb::path_to_uri(p3), "file:///test-._~+!*().txt");

    std::filesystem::path p4("/home/user/../user/doc.txt");
    CHECK_EQ(brothumb::path_to_uri(p4), "file:///home/user/doc.txt");
#else
    // Windows path test
    std::filesystem::path wp("C:\\Users\\user\\my file.png");
    std::string uri = brothumb::path_to_uri(wp);
    CHECK(uri.find("file:///C:/Users/user/my%20file.png") != std::string::npos ||
          uri.find("file:///c:/Users/user/my%20file.png") != std::string::npos);
#endif

    // 5. URI round-trip decoding
    std::string test_uri = "file:///home/user/my%20photo%20%231.png";
    std::filesystem::path roundtrip_path = brothumb::uri_to_path(test_uri);
    CHECK_EQ(roundtrip_path.generic_string(), "/home/user/my photo #1.png");

    std::string win_uri = "file:///C:/projects/brothumb/test.cpp";
    std::filesystem::path win_path = brothumb::uri_to_path(win_uri);
    CHECK_EQ(win_path.generic_string(), "C:/projects/brothumb/test.cpp");

    return bttest::finish("test_uri");
}
