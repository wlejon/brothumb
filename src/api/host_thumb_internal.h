#pragma once

#include "embed/embed.h"
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

inline Value typedArrayFrom(ElementKind kind, const void* data, size_t byteLength, uint32_t elementCount) {
    Value arr = ev::createTypedArray(kind, elementCount);
    ev::fillTypedArray(arr, std::span<const uint8_t>(static_cast<const uint8_t*>(data), byteLength));
    return arr;
}

void installThumbOnto(Value thumbObj);
void drainThumbEvents();
void cancelAllPendingThumbRequests();

} // namespace brothumb::api
