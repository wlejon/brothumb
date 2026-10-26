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

// ---- Path gate ------------------------------------------------------------------
//
// Which files a program may read is the host's decision, not brothumb's: the host
// knows which app is running and what it was granted. With no gate set (the
// default), every path is allowed and nothing below changes.

enum class PathAccess { Read, Write };

/// Answers "" to allow, or the sentence the refusal carries (naming what the app
/// lacks and the path). `path` is absolute and lexically normal (UTF-8); the host
/// canonicalizes it (symlinks) before deciding. The gate is called on the JS
/// thread that made the call, synchronously, before any work is queued, so it may
/// block to ask the user; it must be thread-safe.
///
/// The natives that read the source file ask Read for it: get, getSync (a
/// cacheOnly request too, since a cached thumbnail is the file's content),
/// hasFailed and recordFailure (they read its modification time). The thumbnail
/// cache is brothumb's own, not the program's: writing it (invalidate,
/// clearFailure, clearCache) and naming a thumbnail's path (getThumbnailPath)
/// read no source and ask nothing.
///
/// A refusal is an Error whose `message` is exactly the gate's sentence, `code` is
/// "EACCES" and `path` the absolute path asked about: thrown by a synchronous
/// native, the promise rejected with it by get.
using PathGate = std::function<std::string(const std::string& path, PathAccess access)>;

/// Installs `gate` process-wide (an empty function removes it). Safe to call while
/// realms run: calls already inside the old gate finish against it.
void setPathGate(PathGate gate);

} // namespace brothumb::api

using brothumb::api::installThumb;
using brothumb::api::tickThumbAsync;
using brothumb::api::shutdownThumbAsync;
using brothumb::api::setService;
using brothumb::api::getService;
using brothumb::api::setPathGate;
