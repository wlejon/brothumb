#include "api.h"
#include "host_thumb_internal.h"
#include "brothumb/service.h"

#include <mutex>

namespace brothumb::api {

namespace {

std::mutex g_service_mu;
std::unique_ptr<brothumb::ThumbnailService> g_custom_service;

} // namespace

ThumbnailService* getService() {
    std::lock_guard lock(g_service_mu);
    if (!g_custom_service) {
        g_custom_service = ThumbnailService::create();
    }
    return g_custom_service.get();
}

void setService(std::unique_ptr<brothumb::ThumbnailService> svc) {
    std::lock_guard lock(g_service_mu);
    g_custom_service = std::move(svc);
}

Value makeError(const std::string& msg) {
    ev::Persistent text(ev::fromUtf8(msg));
    auto ctor = ev::globalValue("Error");
    if (ctor.found && ev::isFunction(ctor.value)) {
        ev::Persistent c(ctor.value);
        const Value arg = text.get();
        auto r = ev::construct(c.get(), std::span<const Value>(&arg, 1));
        if (!r.thrown) return r.value;
    }
    return text.get();
}

Value ensureBroThumb() {
    ev::Persistent globalThisVal;
    auto gt = ev::globalValue("globalThis");
    if (gt.found && ev::isObject(gt.value)) {
        globalThisVal.set(gt.value);
    }

    ev::Persistent broP;
    auto bro = ev::globalValue("bro");
    if (bro.found && ev::isObject(bro.value)) broP.set(bro.value);
    if (!ev::isObject(broP.get()) && ev::isObject(globalThisVal.get())) {
        Value candidate = ev::getProperty(globalThisVal.get(), "bro");
        if (ev::isObject(candidate)) broP.set(candidate);
    }
    if (!ev::isObject(broP.get())) {
        broP.set(ev::createObject());
        ev::registerGlobal("bro", broP.get());
        if (ev::isObject(globalThisVal.get())) {
            globalThisVal.set(ev::setProperty(globalThisVal.get(), "bro", broP.get()));
        }
    }

    ev::Persistent thumbP(ev::getProperty(broP.get(), "thumb"));
    if (!ev::isObject(thumbP.get())) {
        thumbP.set(ev::createObject());
        broP.set(ev::setProperty(broP.get(), "thumb", thumbP.get()));
    }
    return thumbP.get();
}

void installThumb() {
    ev::Persistent thumbObj(ensureBroThumb());
    installThumbOnto(thumbObj.get());
}

void tickThumbAsync() {
    drainThumbEvents();
}

void shutdownThumbAsync() {
    cancelAllPendingThumbRequests();
}

} // namespace brothumb::api
