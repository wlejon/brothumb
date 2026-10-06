#pragma once

#include <functional>
#include <memory>
#include <string>

namespace brothumb {
class ThumbnailService;
}

namespace brothumb::api {

/// Mounts `bro.thumb` in the current Bronze realm.
void installThumb();

/// Pumps async thumbnail generation events and settles Promises on the JS thread.
void tickThumbAsync();

/// Cancels active thumbnail requests and cleans up async state.
void shutdownThumbAsync();

/// Sets the thumbnail service used by the API (defaults to ThumbnailService::create()).
void setService(std::unique_ptr<brothumb::ThumbnailService> svc);

/// Gets the thumbnail service currently used by the API.
brothumb::ThumbnailService* getService();

} // namespace brothumb::api

using brothumb::api::installThumb;
using brothumb::api::tickThumbAsync;
using brothumb::api::shutdownThumbAsync;
using brothumb::api::setService;
using brothumb::api::getService;
