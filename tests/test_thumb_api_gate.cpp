// bro.thumb under a path gate (api.h, "Path gate"): the natives that read a
// source file ask the gate first, a refusal is an Error carrying the gate's
// sentence, code EACCES and the path, the cache's own writes ask nothing, and
// with the gate removed everything behaves as before. Run from
// test_thumb_api.cpp's main against its realm and service.

#include "../src/api/api.h"
#include "check.h"
#include "embed/embed.h"
#include "eval/eval.h"

#include <broimage/encode.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ev = bronze::embed;

namespace {

void writePng(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::vector<uint8_t> rgba(64 * 64 * 4, 90);
    std::vector<uint8_t> png;
    broimage::encode_png_memory(png, rgba.data(), 64, 64, 4);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

std::string u8(const fs::path& p) {
    std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string gatePath(const fs::path& p) { return u8(fs::absolute(p).lexically_normal()); }

std::string q(const fs::path& p) {
    std::string out = "'";
    for (char c : p.string()) {
        if (c == '\\' || c == '\'') out += '\\';
        out += c;
    }
    return out + "'";
}

std::string refusal(const fs::path& p) {
    std::string path = gatePath(p);
    return "denied|no files access: " + path + "|EACCES|" + path;
}

std::string evalString(const std::string& src) {
    auto r = bronze::eval::evalScript(src);
    if (r.thrown) return "<thrown>";
    return ev::isString(r.value) ? ev::toUtf8(r.value) : "<not a string>";
}

const char* kDescribe =
    "v => 'ok:' + (v && typeof v === 'object' ? (v.width > 0 ? 'image' : 'empty') : String(v))";

std::string syncJs(const std::string& expr) {
    return evalString(std::string("(function() { const d = ") + kDescribe + ";\n"
                      "  try { return d(" + expr + "); }\n"
                      "  catch (e) { return 'denied|' + e.message + '|' + e.code + '|' + e.path; } })()");
}

std::string awaitJs(const std::string& expr) {
    bronze::eval::evalScript(std::string("globalThis._gateOut = undefined;\n"
                             "Promise.resolve().then(() => ") + expr + ").then(\n" + kDescribe + ",\n"
                             "  e => 'denied|' + e.message + '|' + e.code + '|' + e.path)\n"
                             ".then(s => { globalThis._gateOut = s; });");
    std::string out = "<timeout>";
    bttest::wait_until([&] {
        brothumb::api::tickThumbAsync();
        ev::drainMicrotasks();
        std::string s = evalString("typeof globalThis._gateOut === 'string' ? globalThis._gateOut : ''");
        if (s.empty()) return false;
        out = s;
        return true;
    }, std::chrono::milliseconds(10000));
    return out;
}

} // namespace

void runPathGateTests(const fs::path& tmp) {
    std::cout << "Testing the path gate..." << std::endl;

    const fs::path denied = tmp / "gate" / "denied" / "x.png";
    const fs::path allowed = tmp / "gate" / "ok" / "y.png";
    writePng(denied);
    writePng(allowed);
    const std::string deniedDir = gatePath(denied.parent_path());

    std::atomic<int> calls{0};
    brothumb::api::setPathGate([&](const std::string& path, brothumb::api::PathAccess access) -> std::string {
        ++calls;
        if (access != brothumb::api::PathAccess::Read) return "unexpected write: " + path;
        if (path.compare(0, deniedDir.size(), deniedDir) == 0) return "no files access: " + path;
        return "";
    });

    CHECK_EQ(syncJs("bro.thumb.getSync(" + q(denied) + ")"), refusal(denied));
    CHECK_EQ(syncJs("bro.thumb.getSync(" + q(allowed) + ")"), std::string("ok:image"));
    CHECK_EQ(awaitJs("bro.thumb.get(" + q(denied) + ")"), refusal(denied));
    CHECK_EQ(awaitJs("bro.thumb.get(" + q(denied) + ", { cacheOnly: true })"), refusal(denied));
    CHECK_EQ(awaitJs("bro.thumb.get(" + q(allowed) + ", { force: true })"), std::string("ok:image"));
    CHECK_EQ(syncJs("bro.thumb.hasFailed(" + q(denied) + ")"), refusal(denied));
    CHECK_EQ(syncJs("bro.thumb.recordFailure(" + q(denied) + ")"), refusal(denied));
    CHECK_EQ(syncJs("bro.thumb.hasFailed(" + q(allowed) + ")"), std::string("ok:false"));

    // The cache is brothumb's own: dropping an entry and naming a thumbnail's
    // path read no source and ask nothing.
    const int beforeCache = calls.load();
    CHECK_EQ(syncJs("typeof bro.thumb.invalidate(" + q(denied) + ")"), std::string("ok:boolean"));
    CHECK_EQ(syncJs("typeof bro.thumb.clearFailure(" + q(denied) + ")"), std::string("ok:boolean"));
    CHECK_EQ(syncJs("typeof bro.thumb.getThumbnailPath(" + q(denied) + ")"), std::string("ok:string"));
    CHECK_EQ(calls.load(), beforeCache);

    // With the gate removed, nothing asks and everything works as before.
    brothumb::api::setPathGate(nullptr);
    const int before = calls.load();
    CHECK_EQ(syncJs("bro.thumb.getSync(" + q(denied) + ")"), std::string("ok:image"));
    CHECK_EQ(syncJs("bro.thumb.hasFailed(" + q(denied) + ")"), std::string("ok:false"));
    CHECK_EQ(calls.load(), before);

    std::cout << "  path gate [PASS]" << std::endl;
}
