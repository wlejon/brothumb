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

/// Gets the thumbnail service currently used by the API, making the default
/// one on first use.
brothumb::ThumbnailService* getService();

/// The service if one exists, else null; never makes one. The frame tick
/// uses it, so a page that never asks for a thumbnail starts no workers.
brothumb::ThumbnailService* existingService();

// ---- Access -----------------------------------------------------------------------
//
// Which files a program may read is the host's decision, not brothumb's: the
// natives that read a source file ask the host's one access gate,
// bronze::embed::checkAccess (embed/embed_access.h), with AccessKind::Read and the
// path, absolute and lexically normal (UTF-8; the host canonicalizes it), on the
// JS thread that made the call, synchronously, before any work is queued. With no
// gate installed every path is allowed and nothing below changes.
//
// Read of the source file: get, getSync (a cacheOnly request too, since a cached
// thumbnail is the file's content), hasFailed and recordFailure (they read its
// modification time). The thumbnail cache is brothumb's own, not the program's:
// writing it (invalidate, clearFailure, clearCache) and naming a thumbnail's path
// (getThumbnailPath) read no source and ask nothing.
//
// A refusal is bronze::embed::newAccessError: an Error whose `message` is the
// gate's sentence (named PermissionRevoked for a grant taken back), `code` is
// "EACCES" and `path` the absolute path asked about: thrown by a synchronous
// native, the promise rejected with it by get.

} // namespace brothumb::api

using brothumb::api::installThumb;
using brothumb::api::tickThumbAsync;
using brothumb::api::shutdownThumbAsync;
using brothumb::api::setService;
using brothumb::api::getService;
