#pragma once

#include "api.h"
#include "embed/embed.h"

#include <filesystem>
#include <string>
#include "object_builder.h"
#include "arg_reader.h"
#include "host_class.h"
#include "brothumb/common.h"
#include "brothumb/service.h"

namespace brothumb::api {

namespace ev = bronze::embed;
using Value = bronze::Value;
using ElementKind = bronze::ElementKind;

Value makeError(const std::string& msg);

// The path gate's helpers (access_gate.cpp; the gate itself is in api.h).
struct PathRefusal {
    std::string message;  // the gate's sentence; empty when allowed
    std::string path;     // the absolute path the gate was asked about
    explicit operator bool() const { return !message.empty(); }
};
/// Asks the gate about `p` (made absolute and lexically normal).
PathRefusal checkPath(const std::filesystem::path& p, PathAccess access);
/// The refusal as an Error: message, code "EACCES", path. Fresh: root it.
Value newAccessError(const PathRefusal& refusal);
/// Throws newAccessError into the running program.
Value throwAccessError(const PathRefusal& refusal);

inline Value typedArrayFrom(ElementKind kind, const void* data, size_t byteLength, uint32_t elementCount) {
    Value arr = ev::createTypedArray(kind, elementCount);
    ev::fillTypedArray(arr, std::span<const uint8_t>(static_cast<const uint8_t*>(data), byteLength));
    return arr;
}

void installThumbOnto(Value thumbObj);
void drainThumbEvents();
void cancelAllPendingThumbRequests();

} // namespace brothumb::api
