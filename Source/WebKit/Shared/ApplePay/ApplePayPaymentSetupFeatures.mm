/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "ApplePayPaymentSetupFeaturesWebKit.h"

#if ENABLE(APPLE_PAY)

#import "ArgumentCodersCocoa.h"
#import "Decoder.h"
#import "Encoder.h"
#import <WebCore/ApplePaySetupFeatureWebCore.h>

#import <pal/cocoa/PassKitSoftLink.h>

namespace WebKit {

static RetainPtr<NSArray<PKPaymentSetupFeature *>> toPlatformFeatures(Vector<Ref<WebCore::ApplePaySetupFeature>>&& features)
{
    RetainPtr platformFeatures = adoptNS([[NSMutableArray alloc] initWithCapacity:features.size()]);
    for (auto& feature : features) {
        [platformFeatures addObject:feature->platformFeature()];
    }
    return platformFeatures;
}

PaymentSetupFeatures::PaymentSetupFeatures(Vector<Ref<WebCore::ApplePaySetupFeature>>&& features)
    : m_platformFeatures { toPlatformFeatures(WTFMove(features)) }
{
}

PaymentSetupFeatures::PaymentSetupFeatures(RetainPtr<NSArray>&& platformFeatures)
    : m_platformFeatures { WTFMove(platformFeatures) }
{
}

PaymentSetupFeatures::operator Vector<Ref<WebCore::ApplePaySetupFeature>>() const
{
    Vector<Ref<WebCore::ApplePaySetupFeature>> features;
    features.reserveInitialCapacity([m_platformFeatures count]);
    for (PKPaymentSetupFeature *platformFeature in m_platformFeatures.get()) {
        if (WebCore::ApplePaySetupFeature::supportsFeature(platformFeature))
            features.append(WebCore::ApplePaySetupFeature::create(platformFeature));
    }
    return features;
}

} // namespace WebKit

#endif // ENABLE(APPLE_PAY)
