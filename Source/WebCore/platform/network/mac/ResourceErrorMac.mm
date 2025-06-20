/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#import "config.h"
#import "ResourceError.h"

#import <CoreFoundation/CFError.h>
#import <Foundation/Foundation.h>
#import <pal/spi/cocoa/NetworkSPI.h>
#import <wtf/BlockObjCExceptions.h>
#import <wtf/NeverDestroyed.h>
#import <wtf/URL.h>
#import <wtf/cocoa/TypeCastsCocoa.h>
#import <wtf/text/WTFString.h>

@interface NSError (WebExtras)
- (NSString *)_web_localizedDescription;
@end

#if PLATFORM(IOS_FAMILY)

// This workaround code exists here because we can't call translateToCFError in Foundation. Once we
// have that, we can remove this code. <rdar://problem/9837415> Need SPI for translateCFError
// The code is mostly identical to Foundation - I changed the class name and fixed minor compile errors.
// We need this because client code (Safari) wants an NSError with NSURLErrorDomain as its domain.
// The Foundation code below does that and sets up appropriate certificate keys in the NSError.

@interface WebCustomNSURLError : NSError

@end

@implementation WebCustomNSURLError

static NSDictionary* dictionaryThatCanCode(NSDictionary* src)
{
    // This function makes a copy of input dictionary, modifies it such that it "should" (as much as we can help it)
    // not contain any objects that do not conform to NSCoding protocol, and returns it autoreleased.

    auto dst = adoptNS([src mutableCopy]);

    // Kill the known problem entries.
    [dst removeObjectForKey:@"NSErrorPeerCertificateChainKey"]; // NSArray with SecCertificateRef objects
    [dst removeObjectForKey:@"NSErrorClientCertificateChainKey"]; // NSArray with SecCertificateRef objects
    [dst removeObjectForKey:NSURLErrorFailingURLPeerTrustErrorKey]; // SecTrustRef object
    [dst removeObjectForKey:NSUnderlyingErrorKey]; // (Immutable) CFError containing kCF equivalent of the above
    // We could reconstitute this but it's more trouble than it's worth

    // Non-comprehensive safety check:  Kill top-level dictionary entries that don't conform to NSCoding.
    // We may hit ones we just removed, but that's fine.
    // We don't handle arbitrary objects that clients have stuffed into the dictionary, since we may not know how to
    // get at its conents (e.g., a CFError object -- you'd have to know it had a userInfo dictionary and kill things
    // inside it).
    [src enumerateKeysAndObjectsUsingBlock:^(id key, id obj, BOOL*) {
        if (! [obj conformsToProtocol:@protocol(NSCoding)]) {
            [dst removeObjectForKey:key];
        }
        // FIXME: We could drill down into subdictionaries, but it seems more trouble than it's worth
    }];

    return dst.autorelease();
}

- (void)encodeWithCoder:(NSCoder *)coder
{
    NSDictionary* newUserInfo = dictionaryThatCanCode([self userInfo]);

    [[NSError errorWithDomain:[self domain] code:[self code] userInfo:newUserInfo] encodeWithCoder:coder];
}

@end

#endif // PLATFORM(IOS_FAMILY)

namespace WebCore {

static RetainPtr<NSError> createNSErrorFromResourceErrorBase(const ResourceErrorBase& resourceError)
{
    RetainPtr<NSMutableDictionary> userInfo = adoptNS([[NSMutableDictionary alloc] init]);

    if (!resourceError.localizedDescription().isEmpty())
        [userInfo setValue:resourceError.localizedDescription().createNSString().get() forKey:NSLocalizedDescriptionKey];

    if (!resourceError.failingURL().isEmpty()) {
        [userInfo setValue:resourceError.failingURL().string().createNSString().get() forKey:@"NSErrorFailingURLStringKey"];
        if (RetainPtr cocoaURL = resourceError.failingURL().createNSURL())
            [userInfo setValue:cocoaURL.get() forKey:NSURLErrorFailingURLErrorKey];
    }

    return adoptNS([[NSError alloc] initWithDomain:resourceError.domain().createNSString().get() code:resourceError.errorCode() userInfo:userInfo.get()]);
}

ResourceError::ResourceError(NSError *nsError)
    : ResourceErrorBase(Type::Null)
    , m_platformError(nsError)
    , m_dataIsUpToDate(false)
{
    mapPlatformError();
}

ResourceError ResourceError::fromIPCData(std::optional<IPCData>&& ipcData)
{
    if (!ipcData)
        return { };

    ResourceError error(ipcData->nsError.get());
    error.setType(ipcData->type);
    if (ipcData->isSanitized)
        error.setAsSanitized();
    return error;
}

auto ResourceError::ipcData() const -> std::optional<IPCData>
{
    if (isNull())
        return std::nullopt;

    return IPCData {
        type(),
        nsError(),
        isSanitized()
    };
}

ResourceError::ResourceError(CFErrorRef cfError)
    : ResourceError { (__bridge NSError *)cfError }
{
}

const String& ResourceError::getNSURLErrorDomain() const
{
    static const NeverDestroyed<String> errorDomain(NSURLErrorDomain);
    return errorDomain.get();
}

const String& ResourceError::getCFErrorDomainCFNetwork() const
{
    static const NeverDestroyed<String> errorDomain(kCFErrorDomainCFNetwork);
    return errorDomain.get();
}

ResourceError::ErrorRecoveryMethod ResourceError::errorRecoveryMethod() const
{
    lazyInit();

    bool isRecoverableError { false };
    RetainPtr nsDomain = m_domain.createNSString();
    if ([nsDomain isEqualToString:NSURLErrorDomain]) {
        switch (m_errorCode) {
        case NSURLErrorTimedOut:
        case NSURLErrorCannotFindHost:
        case NSURLErrorCannotConnectToHost:
        case NSURLErrorNetworkConnectionLost:
        case NSURLErrorHTTPTooManyRedirects:
        case NSURLErrorResourceUnavailable:
        case NSURLErrorNotConnectedToInternet:
        case NSURLErrorRedirectToNonExistentLocation:
        case NSURLErrorBadServerResponse:
        case NSURLErrorZeroByteResource:
        case NSURLErrorCannotDecodeRawData:
        case NSURLErrorCannotDecodeContentData:
        case NSURLErrorCannotParseResponse:
        case NSURLErrorSecureConnectionFailed:
        case NSURLErrorServerCertificateHasBadDate:
        case NSURLErrorServerCertificateUntrusted:
        case NSURLErrorServerCertificateHasUnknownRoot:
        case NSURLErrorServerCertificateNotYetValid:
        case NSURLErrorClientCertificateRejected:
        case NSURLErrorClientCertificateRequired:
            isRecoverableError = true;
        }
    } else if ([nsDomain isEqualToString:@"WebKitErrorDomain"]) {
        // FIXME: These literals should be moved into a central location that is shared with WebKit::API.
        constexpr auto httpsUpgradeRedirectLoop { 304 };
        isRecoverableError = m_errorCode == httpsUpgradeRedirectLoop;
    }

    if (isRecoverableError && m_failingURL.protocolIs("https"_s))
        return ResourceError::ErrorRecoveryMethod::HTTPFallback;
    return ResourceError::ErrorRecoveryMethod::NoRecovery;
}

void ResourceError::mapPlatformError()
{
    static_assert(static_cast<NSInteger>(NSURLErrorTimedOut) == static_cast<NSInteger>(kCFURLErrorTimedOut), "NSURLErrorTimedOut needs to equal kCFURLErrorTimedOut");
    static_assert(static_cast<NSInteger>(NSURLErrorCancelled) == static_cast<NSInteger>(kCFURLErrorCancelled), "NSURLErrorCancelled needs to equal kCFURLErrorCancelled");

    if (!m_platformError)
        return;

    auto domain = [m_platformError domain];
    auto errorCode = [m_platformError code];

    if ([domain isEqualToString:NSURLErrorDomain] || [domain isEqualToString:(__bridge NSString *)kCFErrorDomainCFNetwork])
        setType((errorCode == NSURLErrorTimedOut) ? Type::Timeout : (errorCode == NSURLErrorCancelled) ? Type::Cancellation : Type::General);
    else
        setType(Type::General);
}

void ResourceError::platformLazyInit()
{
    if (m_dataIsUpToDate)
        return;

    m_domain = [m_platformError domain];
    m_errorCode = [m_platformError code];

    RetainPtr userInfo = [m_platformError userInfo];
    if (auto *failingURLString = dynamic_objc_cast<NSString>([userInfo valueForKey:@"NSErrorFailingURLStringKey"]))
        m_failingURL = URL { failingURLString };
    else if (auto *failingURL = dynamic_objc_cast<NSURL>([userInfo valueForKey:NSURLErrorFailingURLErrorKey]))
        m_failingURL = URL { failingURL };
    // Workaround for <rdar://problem/6554067>
    m_localizedDescription = m_failingURL.string();
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    m_localizedDescription = [m_platformError _web_localizedDescription];
    END_BLOCK_OBJC_EXCEPTIONS

    m_dataIsUpToDate = true;
}

bool ResourceError::platformCompare(const ResourceError& a, const ResourceError& b)
{
    return a.nsError() == b.nsError();
}

void ResourceError::doPlatformIsolatedCopy(const ResourceError&)
{
}

NSError *ResourceError::nsError() const
{
    if (isNull()) {
        ASSERT(!m_platformError);
        return nil;
    }

    if (!m_platformError)
        m_platformError = createNSErrorFromResourceErrorBase(*this);

    return m_platformError.get();
}

ResourceError::operator NSError *() const
{
    return nsError();
}

CFErrorRef ResourceError::cfError() const
{
    return (__bridge CFErrorRef)nsError();
}

ResourceError::operator CFErrorRef() const
{
    return cfError();
}

#if ENABLE(ADVANCED_PRIVACY_PROTECTIONS)

bool ResourceError::blockedKnownTracker() const
{
    if (id blockedTrackerFailure = nsError().userInfo[@"_NSURLErrorBlockedTrackerFailureKey"])
        return [blockedTrackerFailure boolValue];
    // This loop can be removed when the CFNetwork loader is no longer in use
    for (NSError *underlyingError in nsError().underlyingErrors) {
        if ([underlyingError.userInfo[@"_NSURLErrorBlockedTrackerFailureKey"] boolValue])
            return true;
    }
    return false;
}

String ResourceError::blockedTrackerHostName() const
{
    ASSERT(blockedKnownTracker());

    if (id failingPath = nsError().userInfo[@"_NSURLErrorNWPathKey"]) {
        auto failingEndpoint = adoptNS(nw_path_copy_effective_remote_endpoint(failingPath));
        if (auto* hostName = nw_endpoint_get_known_tracker_name(failingEndpoint.get()))
            return String::fromUTF8(hostName);
        return { };
    }
    // This loop can be removed when the CFNetwork loader is no longer in use
    for (NSError *underlyingError in nsError().underlyingErrors) {
        if (id failingPath = underlyingError.userInfo[@"_NSURLErrorNWPathKey"]) {
            auto failingEndpoint = adoptNS(nw_path_copy_effective_remote_endpoint(failingPath));
            if (auto* hostName = nw_endpoint_get_known_tracker_name(failingEndpoint.get()))
                return String::fromUTF8(hostName);
        }
    }
    return { };
}

#endif // ENABLE(ADVANCED_PRIVACY_PROTECTIONS)

bool ResourceError::hasMatchingFailingURLKeys() const
{
    if (RetainPtr<id> nsErrorFailingURL = [nsError().userInfo objectForKey:NSURLErrorFailingURLErrorKey]) {
        RetainPtr failingURL = dynamic_objc_cast<NSURL>(nsErrorFailingURL.get());
        if (!failingURL)
            return false;
        if (RetainPtr<id> nsErrorFailingURLString = [nsError().userInfo objectForKey:@"NSErrorFailingURLStringKey"]) {
            RetainPtr failingURLString = dynamic_objc_cast<NSString>(nsErrorFailingURLString.get());
            if (!failingURLString)
                return false;
            if (![failingURL isEqual:URL(failingURLString.get()).createNSURL().get()])
                return false;
        }
    }
    return true;
}

} // namespace WebCore
