/*
 * Copyright (C) 2006-2023 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#import "config.h"
#import <wtf/text/WTFString.h>

#import <CoreFoundation/CFString.h>
#import <wtf/cocoa/TypeCastsCocoa.h>

namespace WTF {

RetainPtr<NSString> String::createNSString() const
{
    if (RefPtr impl = m_impl)
        return impl->createNSString();
    return @"";
}

RetainPtr<id> makeNSArrayElement(const String& vectorElement)
{
    return bridge_cast(vectorElement.createCFString());
}

std::optional<String> makeVectorElement(const String*, id arrayElement)
{
    NSString *nsString = dynamic_objc_cast<NSString>(arrayElement);
    if (!nsString)
        return std::nullopt;
    return { { nsString } };
}

}
