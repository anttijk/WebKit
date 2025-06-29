/*
 * Copyright (C) 2012-2023 Apple Inc. All rights reserved.
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

#if PLATFORM(MAC) || PLATFORM(MACCATALYST)
#import "AuxiliaryProcess.h"

#import "ApplicationServicesSPI.h"
#import "CodeSigning.h"
#import "SandboxInitializationParameters.h"
#import "SandboxUtilities.h"
#import "WKFoundation.h"
#import "XPCServiceEntryPoint.h"
#import <WebCore/SystemVersion.h>
#import <mach-o/dyld.h>
#import <mach/mach.h>
#import <mach/task.h>
#import <pal/crypto/CryptoDigest.h>
#import <pal/spi/cocoa/CoreServicesSPI.h>
#import <pal/spi/cocoa/LaunchServicesSPI.h>
#import <pal/spi/mac/QuarantineSPI.h>
#import <pwd.h>
#import <stdlib.h>
#import <sys/sysctl.h>
#import <sysexits.h>
#import <wtf/DataLog.h>
#import <wtf/FileHandle.h>
#import <wtf/FileSystem.h>
#import <wtf/SafeStrerror.h>
#import <wtf/Scope.h>
#import <wtf/SoftLinking.h>
#import <wtf/StdLibExtras.h>
#import <wtf/SystemTracing.h>
#import <wtf/WTFProcess.h>
#import <wtf/WallTime.h>
#import <wtf/cocoa/Entitlements.h>
#import <wtf/spi/darwin/SandboxSPI.h>
#import <wtf/text/Base64.h>
#import <wtf/text/MakeString.h>
#import <wtf/text/StringBuilder.h>
#import <wtf/text/cf/StringConcatenateCF.h>

#if USE(APPLE_INTERNAL_SDK)
#import <rootless.h>
#endif

#if __has_include(<WebKitAdditions/DyldCallbackAdditions.h>)
#import <WebKitAdditions/DyldCallbackAdditions.h>
#endif

SOFT_LINK_SYSTEM_LIBRARY(libsystem_info)
SOFT_LINK_OPTIONAL(libsystem_info, mbr_close_connections, int, (), ());
SOFT_LINK_OPTIONAL(libsystem_info, lookup_close_connections, int, (), ());

SOFT_LINK_FRAMEWORK_IN_UMBRELLA(ApplicationServices, HIServices)
SOFT_LINK_OPTIONAL(HIServices, HIS_XPC_ResetMessageConnection, void, (), ())

#if PLATFORM(MAC)
#define USE_CACHE_COMPILED_SANDBOX 1
#else
#define USE_CACHE_COMPILED_SANDBOX 0
#endif

extern const char* const SANDBOX_BUILD_ID; // Defined by the Sandbox framework

namespace WebKit {
using namespace WebCore;

#if USE(CACHE_COMPILED_SANDBOX)
using SandboxProfile = typename std::remove_pointer<sandbox_profile_t>::type;
struct SandboxProfileDeleter {
    void operator()(SandboxProfile* ptr)
    {
        sandbox_free_profile(ptr);
    }
};
using SandboxProfilePtr = std::unique_ptr<SandboxProfile, SandboxProfileDeleter>;

using SandboxParameters = typename std::remove_pointer<sandbox_params_t>::type;
struct SandboxParametersDeleter {
    void operator()(SandboxParameters* ptr)
    {
        sandbox_free_params(ptr);
    }
};
using SandboxParametersPtr = std::unique_ptr<SandboxParameters, SandboxParametersDeleter>;

constexpr unsigned guidSize = 36 + 1;
constexpr unsigned versionSize = 31 + 1;

struct CachedSandboxHeader {
    uint32_t versionNumber;
    uint32_t libsandboxVersion;
    uint32_t headerSize;
    uint32_t builtinSize; // If a builtin doesn't exist, this is UINT_MAX.
    uint32_t dataSize;
    std::array<char, guidSize> sandboxBuildID;
    std::array<char, versionSize> osVersion;
};
// The file is layed out on disk like:
// byte 0
// CachedSandboxHeader <- sizeof(CachedSandboxHeader) bytes
// SandboxHeader <- CachedSandboxHeader::headerSize bytes
// [SandboxBuiltin] optional. Present if CachedSandboxHeader::builtinSize is not UINT_MAX. If present, builtinSize bytes (not including null termination).
// SandboxData <- CachedSandboxHeader::dataSize bytes
// byte N

struct SandboxInfo {
    SandboxInfo(const String& parentDirectoryPath, const String& directoryPath, const String& filePath, const SandboxParametersPtr& sandboxParameters, const CString& header, const WTF::AuxiliaryProcessType& processType, const SandboxInitializationParameters& initializationParameters, const String& profileOrProfilePath, bool isProfilePath)
        : parentDirectoryPath { parentDirectoryPath }
        , directoryPath { directoryPath }
        , filePath { filePath }
        , sandboxParameters { sandboxParameters }
        , header { header }
        , processType { processType }
        , initializationParameters { initializationParameters }
        , profileOrProfilePath { profileOrProfilePath }
        , isProfilePath { isProfilePath }
    {
    }

    const String& parentDirectoryPath;
    const String& directoryPath;
    const String& filePath;
    const SandboxParametersPtr& sandboxParameters;
    const CString& header;
    const WTF::AuxiliaryProcessType& processType;
    const SandboxInitializationParameters& initializationParameters;
    const String& profileOrProfilePath;
    const bool isProfilePath;
};

constexpr uint32_t CachedSandboxVersionNumber = 1;
#endif // USE(CACHE_COMPILED_SANDBOX)

void AuxiliaryProcess::launchServicesCheckIn()
{
#if HAVE(CSCHECKFIXDISABLE)
    // _CSCheckFixDisable() needs to be called before checking in with Launch Services.
    _CSCheckFixDisable();
#endif

    _LSSetApplicationLaunchServicesServerConnectionStatus(0, 0);
    RetainPtr<CFDictionaryRef> unused = _LSApplicationCheckIn(kLSDefaultSessionID, CFBundleGetInfoDictionary(CFBundleGetMainBundle()));
}

static OSStatus enableSandboxStyleFileQuarantine()
{
#if !PLATFORM(MACCATALYST)
    qtn_proc_t quarantineProperties = qtn_proc_alloc();
    auto quarantinePropertiesDeleter = makeScopeExit([quarantineProperties]() {
        qtn_proc_free(quarantineProperties);
    });


    if (qtn_proc_init_with_self(quarantineProperties)) {
        // See <rdar://problem/13463752>.
        qtn_proc_init(quarantineProperties);
    }

    if (auto error = qtn_proc_set_flags(quarantineProperties, QTN_FLAG_SANDBOX))
        return error;

    // QTN_FLAG_SANDBOX is silently ignored if security.mac.qtn.sandbox_enforce sysctl is 0.
    // In that case, quarantine falls back to advisory QTN_FLAG_DOWNLOAD.
    return qtn_proc_apply_to_self(quarantineProperties);
#else
    return false;
#endif
}

#if USE(CACHE_COMPILED_SANDBOX)
static std::optional<Vector<uint8_t>> fileContents(const String& path)
{
    auto fileHandle = FileSystem::openFile(path, FileSystem::FileOpenMode::Read);
    if (!fileHandle)
        return std::nullopt;

    std::array<uint8_t, 4096> chunk;
    Vector<uint8_t> contents;
    contents.reserveInitialCapacity(chunk.size());
    while (auto bytesRead = fileHandle.read(chunk).value_or(0))
        contents.append(std::span { chunk }.first(bytesRead));
    contents.shrinkToFit();

    return contents;
}

#if USE(APPLE_INTERNAL_SDK)
// These strings must match the last segment of the "com.apple.rootless.storage.<this part must match>" entry in each
// process's restricted entitlements file (ex. Configurations/Networking-OSX-restricted.entitlements).
constexpr ASCIILiteral processStorageClass(WTF::AuxiliaryProcessType type)
{
    switch (type) {
    case WTF::AuxiliaryProcessType::WebContent:
        return "WebKitWebContentSandbox"_s;
    case WTF::AuxiliaryProcessType::Network:
        return "WebKitNetworkingSandbox"_s;
    case WTF::AuxiliaryProcessType::Plugin:
        return "WebKitPluginSandbox"_s;
#if ENABLE(GPU_PROCESS)
    case WTF::AuxiliaryProcessType::GPU:
        return "WebKitGPUSandbox"_s;
#endif
    }
}
#endif // USE(APPLE_INTERNAL_SDK)

static std::optional<CString> setAndSerializeSandboxParameters(const SandboxInitializationParameters& initializationParameters, const SandboxParametersPtr& sandboxParameters, const String& profileOrProfilePath, bool isProfilePath)
{
    StringBuilder builder;
    for (size_t i = 0; i < initializationParameters.count(); ++i) {
        const char* name = initializationParameters.name(i);
        const char* value = initializationParameters.value(i);
        if (sandbox_set_param(sandboxParameters.get(), name, value)) {
            WTFLogAlways("%s: Could not set sandbox parameter: %s\n", getprogname(), safeStrerror(errno).data());
            CRASH();
        }
        builder.append(unsafeSpan(name), ':', unsafeSpan(value), ':');
    }
    if (isProfilePath) {
        auto contents = fileContents(profileOrProfilePath);
        if (!contents)
            return std::nullopt;
        builder.append(*contents);
    } else
        builder.append(profileOrProfilePath);
    return builder.toString().ascii();
}

static String sandboxDataVaultParentDirectory()
{
    char temp[PATH_MAX];
    size_t length = confstr(_CS_DARWIN_USER_CACHE_DIR, temp, sizeof(temp));
    if (!length) {
        WTFLogAlways("%s: Could not retrieve user temporary directory path: %s\n", getprogname(), safeStrerror(errno).data());
        exitProcess(EX_NOPERM);
    }
    RELEASE_ASSERT(length <= sizeof(temp));
    char resolvedPath[PATH_MAX];
    if (!realpath(temp, resolvedPath)) {
        WTFLogAlways("%s: Could not canonicalize user temporary directory path: %s\n", getprogname(), safeStrerror(errno).data());
        exitProcess(EX_NOPERM);
    }
    return String::fromUTF8(resolvedPath);
}

static String sandboxDirectory(WTF::AuxiliaryProcessType processType, const String& parentDirectory)
{
    StringBuilder directory;
    directory.append(parentDirectory);
    switch (processType) {
    case WTF::AuxiliaryProcessType::WebContent:
        directory.append("/com.apple.WebKit.WebContent.Sandbox"_s);
        break;
    case WTF::AuxiliaryProcessType::Network:
        directory.append("/com.apple.WebKit.Networking.Sandbox"_s);
        break;
    case WTF::AuxiliaryProcessType::Plugin:
        WTFLogAlways("sandboxDirectory: Unexpected Plugin process initialization.");
        CRASH();
        break;
#if ENABLE(GPU_PROCESS)
    case WTF::AuxiliaryProcessType::GPU:
        directory.append("/com.apple.WebKit.GPU.Sandbox"_s);
        break;
#endif
    }

#if !USE(APPLE_INTERNAL_SDK)
    // Add .OpenSource suffix so that open source builds don't try to access a data vault used by system Safari.
    directory.append(".OpenSource"_s);
#endif

    return directory.toString();
}

static String sandboxFilePath(const String& directoryPath)
{
    return makeString(directoryPath, "/CompiledSandbox"_s);
}

static bool ensureSandboxCacheDirectory(const SandboxInfo& info)
{
    if (FileSystem::fileTypeFollowingSymlinks(info.parentDirectoryPath) != FileSystem::FileType::Directory) {
        FileSystem::makeAllDirectories(info.parentDirectoryPath);
        if (FileSystem::fileTypeFollowingSymlinks(info.parentDirectoryPath) != FileSystem::FileType::Directory) {
            WTFLogAlways("%s: Could not create sandbox directory\n", getprogname());
            return false;
        }
    }

#if USE(APPLE_INTERNAL_SDK)
    auto storageClass = processStorageClass(info.processType);
    CString directoryPath = FileSystem::fileSystemRepresentation(info.directoryPath);
    if (directoryPath.isNull())
        return false;

    auto makeDataVault = [&] {
        do {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            if (!rootless_mkdir_datavault(directoryPath.data(), 0700, storageClass))
                return true;
ALLOW_DEPRECATED_DECLARATIONS_END
        } while (errno == EAGAIN);
        return false;
    };

    if (makeDataVault())
        return true;

    if (errno == EEXIST) {
        // The directory already exists. First we'll check if it is a data vault. If it is then
        // we are the ones who created it and we can continue. If it is not a datavault then we'll just
        // delete it and try to make a new one.
        if (!rootless_check_datavault_flag(directoryPath.data(), storageClass))
            return true;

        if (FileSystem::fileType(info.directoryPath) == FileSystem::FileType::Directory) {
            if (!FileSystem::deleteNonEmptyDirectory(info.directoryPath))
                return false;
        } else {
            if (!FileSystem::deleteFile(info.directoryPath))
                return false;
        }

        if (!makeDataVault())
            return false;
    } else {
        WTFLogAlways("%s: Sandbox directory couldn't be created: ", getprogname(), safeStrerror(errno).data());
        return false;
    }
#else
    bool hasSandboxDirectory = FileSystem::fileTypeFollowingSymlinks(info.directoryPath) == FileSystem::FileType::Directory;
    if (!hasSandboxDirectory) {
        if (FileSystem::makeAllDirectories(info.directoryPath)) {
            ASSERT(FileSystem::fileTypeFollowingSymlinks(info.directoryPath) == FileSystem::FileType::Directory);
            hasSandboxDirectory = true;
        } else {
            // We may have raced with someone else making it. That's ok.
            hasSandboxDirectory = FileSystem::fileTypeFollowingSymlinks(info.directoryPath) == FileSystem::FileType::Directory;
        }
    }

    if (!hasSandboxDirectory) {
        // Bailing because we don't have a sandbox directory.
        return false;
    }
#endif // USE(APPLE_INTERNAL_SDK)

    return true;
}

static bool writeSandboxDataToCacheFile(const SandboxInfo& info, const Vector<uint8_t>& cacheFile)
{
    // To avoid locking, write the sandbox data to a temporary path including the current process' PID
    // then rename it to the final cache path.
    auto temporaryPath = makeString(info.filePath, '-', getpid());
    auto fileHandle = FileSystem::openFile(temporaryPath, FileSystem::FileOpenMode::Truncate);
    if (fileHandle.write(cacheFile.span()) != cacheFile.size()) {
        FileSystem::deleteFile(temporaryPath);
        return false;
    }
    if (!FileSystem::moveFile(temporaryPath, info.filePath)) {
        FileSystem::deleteFile(temporaryPath);
        return false;
    }
    return true;
}

static SandboxProfilePtr compileAndCacheSandboxProfile(const SandboxInfo& info)
{
    if (!ensureSandboxCacheDirectory(info))
        return nullptr;

    char* error = nullptr;
    CString profileOrProfilePath = info.isProfilePath ? FileSystem::fileSystemRepresentation(info.profileOrProfilePath) : info.profileOrProfilePath.utf8();
    if (profileOrProfilePath.isNull())
        return nullptr;
    SandboxProfilePtr sandboxProfile { info.isProfilePath ? sandbox_compile_file(profileOrProfilePath.data(), info.sandboxParameters.get(), &error) : sandbox_compile_string(profileOrProfilePath.data(), info.sandboxParameters.get(), &error) };
    if (!sandboxProfile) {
        WTFLogAlways("%s: Could not compile WebContent sandbox: %s\n", getprogname(), error);
        return nullptr;
    }

    const bool haveBuiltin = sandboxProfile->builtin;
    int32_t libsandboxVersion = NSVersionOfRunTimeLibrary("sandbox");
    RELEASE_ASSERT(libsandboxVersion > 0);
    String osVersion = systemMarketingVersion();

    CachedSandboxHeader cachedHeader {
        CachedSandboxVersionNumber,
        static_cast<uint32_t>(libsandboxVersion),
        safeCast<uint32_t>(info.header.length()),
        haveBuiltin ? safeCast<uint32_t>(unsafeSpan(sandboxProfile->builtin).size()) : std::numeric_limits<uint32_t>::max(),
        safeCast<uint32_t>(sandboxProfile->size),
        { 0 },
        { 0 }
    };

    auto sandboxBuildID = unsafeSpanIncludingNullTerminator(SANDBOX_BUILD_ID);
    memcpySpan(std::span { cachedHeader.sandboxBuildID }, sandboxBuildID);

    auto osVersionUTF8 = osVersion.utf8();
    memcpySpan(std::span { cachedHeader.osVersion }, osVersionUTF8.spanIncludingNullTerminator());

    const size_t expectedFileSize = sizeof(cachedHeader) + cachedHeader.headerSize + (haveBuiltin ? cachedHeader.builtinSize : 0) + cachedHeader.dataSize;

    Vector<uint8_t> cacheFile;
    cacheFile.reserveInitialCapacity(expectedFileSize);
    cacheFile.append(asByteSpan(cachedHeader));
    cacheFile.append(info.header.span());
    if (haveBuiltin)
        cacheFile.append(unsafeMakeSpan(sandboxProfile->builtin, cachedHeader.builtinSize));
    cacheFile.append(unsafeMakeSpan(sandboxProfile->data, cachedHeader.dataSize));

    if (!writeSandboxDataToCacheFile(info, cacheFile))
        WTFLogAlways("%s: Unable to cache compiled sandbox\n", getprogname());

    return sandboxProfile;
}

static bool tryApplyCachedSandbox(const SandboxInfo& info)
{
#if USE(APPLE_INTERNAL_SDK)
    CString directoryPath = FileSystem::fileSystemRepresentation(info.directoryPath);
    if (directoryPath.isNull())
        return false;
    if (rootless_check_datavault_flag(directoryPath.data(), processStorageClass(info.processType)))
        return false;
#endif

    auto contents = fileContents(info.filePath);
    if (!contents || contents->isEmpty())
        return false;
    Vector<uint8_t> cachedSandboxContents = WTFMove(*contents);
    if (sizeof(CachedSandboxHeader) > cachedSandboxContents.size())
        return false;

    // This data may be corrupted if the sandbox file was cached on a different platform with different endianness
    CachedSandboxHeader cachedSandboxHeader;
    memcpySpan(asMutableByteSpan(cachedSandboxHeader), cachedSandboxContents.span().first(sizeof(CachedSandboxHeader)));
    int32_t libsandboxVersion = NSVersionOfRunTimeLibrary("sandbox");
    RELEASE_ASSERT(libsandboxVersion > 0);
    String osVersion = systemMarketingVersion();

    if (cachedSandboxHeader.versionNumber != CachedSandboxVersionNumber)
        return false;
    if (static_cast<uint32_t>(libsandboxVersion) != cachedSandboxHeader.libsandboxVersion)
        return false;
    if (!equalSpans(std::span { cachedSandboxHeader.sandboxBuildID }, unsafeSpanIncludingNullTerminator(SANDBOX_BUILD_ID)))
        return false;
    if (StringView::fromLatin1(cachedSandboxHeader.osVersion.data()) != osVersion)
        return false;

    const bool haveBuiltin = cachedSandboxHeader.builtinSize != std::numeric_limits<uint32_t>::max();

    // These values are computed based on the disk layout specified below the definition of the CachedSandboxHeader struct
    // and must be changed if the layout changes.
    auto sandboxHeader = cachedSandboxContents.mutableSpan().subspan(sizeof(CachedSandboxHeader));
    auto sandboxBuiltin = sandboxHeader.subspan(cachedSandboxHeader.headerSize);
    auto sandboxData = haveBuiltin ? sandboxBuiltin.subspan(cachedSandboxHeader.builtinSize) : sandboxBuiltin;

    size_t expectedFileSize = sizeof(CachedSandboxHeader) + cachedSandboxHeader.headerSize + cachedSandboxHeader.dataSize;
    if (haveBuiltin)
        expectedFileSize += cachedSandboxHeader.builtinSize;
    if (cachedSandboxContents.size() != expectedFileSize)
        return false;
    if (cachedSandboxHeader.headerSize != info.header.length())
        return false;
    if (!spanHasPrefix(sandboxHeader, info.header.span()))
        return false;

    SandboxProfile profile { };
    CString builtin;
    profile.builtin = nullptr;
    profile.size = cachedSandboxHeader.dataSize;
    if (haveBuiltin) {
        std::span<char> cstringBuffer;
        builtin = CString::newUninitialized(cachedSandboxHeader.builtinSize, cstringBuffer);
        profile.builtin = cstringBuffer.data();
        if (builtin.isNull())
            return false;
        memcpySpan(builtin.mutableSpan(), sandboxBuiltin.first(cachedSandboxHeader.builtinSize));
    }
    ASSERT(sandboxData.subspan(profile.size).data() <= std::to_address(cachedSandboxContents.end()));
    profile.data = sandboxData.data();

    if (sandbox_apply(&profile)) {
        WTFLogAlways("%s: Could not apply cached sandbox: %s\n", getprogname(), safeStrerror(errno).data());
        return false;
    }

    return true;
}
#endif // USE(CACHE_COMPILED_SANDBOX)

static inline const NSBundle *webKit2Bundle()
{
    const static NeverDestroyed<RetainPtr<NSBundle>> bundle = [NSBundle bundleForClass:NSClassFromString(@"WKWebView")];
    return bundle.get().get();
}

static void getSandboxProfileOrProfilePath(const SandboxInitializationParameters& parameters, String& profileOrProfilePath, bool& isProfilePath)
{
    switch (parameters.mode()) {
    case SandboxInitializationParameters::ProfileSelectionMode::UseDefaultSandboxProfilePath:
        profileOrProfilePath = [webKit2Bundle() pathForResource:[[NSBundle mainBundle] bundleIdentifier] ofType:@"sb"];
        isProfilePath = true;
        return;
    case SandboxInitializationParameters::ProfileSelectionMode::UseOverrideSandboxProfilePath:
        profileOrProfilePath = parameters.overrideSandboxProfilePath();
        isProfilePath = true;
        return;
    case SandboxInitializationParameters::ProfileSelectionMode::UseSandboxProfile:
        profileOrProfilePath = parameters.sandboxProfile();
        isProfilePath = false;
        return;
    }
}

static bool compileAndApplySandboxSlowCase(const String& profileOrProfilePath, bool isProfilePath, const SandboxInitializationParameters& parameters)
{
    char* errorBuf;
    CString temp = isProfilePath ? FileSystem::fileSystemRepresentation(profileOrProfilePath) : profileOrProfilePath.utf8();
    uint64_t flags = isProfilePath ? SANDBOX_NAMED_EXTERNAL : 0;

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (sandbox_init_with_parameters(temp.data(), flags, parameters.namedParameterVector().span().data(), &errorBuf)) {
ALLOW_DEPRECATED_DECLARATIONS_END
        WTFLogAlways("%s: Could not initialize sandbox profile [%s], error '%s'\n", getprogname(), temp.data(), errorBuf);
        for (size_t i = 0, count = parameters.count(); i != count; ++i)
            WTFLogAlways("%s=%s\n", parameters.name(i), parameters.value(i));
        return false;
    }
    return true;
}

static bool applySandbox(const AuxiliaryProcessInitializationParameters& parameters, const SandboxInitializationParameters& sandboxInitializationParameters, const String& dataVaultParentDirectory)
{
    String profileOrProfilePath;
    bool isProfilePath;
    getSandboxProfileOrProfilePath(sandboxInitializationParameters, profileOrProfilePath, isProfilePath);
    if (profileOrProfilePath.isEmpty()) {
        WTFLogAlways("%s: Profile path is invalid\n", getprogname());
        CRASH();
    }

    AuxiliaryProcess::setNotifyOptions();

#if USE(CACHE_COMPILED_SANDBOX)
    // The plugin process's DARWIN_USER_TEMP_DIR and DARWIN_USER_CACHE_DIR sandbox parameters are randomized so
    // so the compiled sandbox should not be cached because it won't be reused.
    if (parameters.processType == WTF::AuxiliaryProcessType::Plugin) {
        WTFLogAlways("applySandbox: Unexpected Plugin process initialization.");
        CRASH();
    }

    SandboxParametersPtr sandboxParameters { sandbox_create_params() };
    if (!sandboxParameters) {
        WTFLogAlways("%s: Could not create sandbox parameters\n", getprogname());
        CRASH();
    }
    auto header = setAndSerializeSandboxParameters(sandboxInitializationParameters, sandboxParameters, profileOrProfilePath, isProfilePath);
    if (!header) {
        WTFLogAlways("%s: Sandbox parameters are invalid\n", getprogname());
        CRASH();
    }

    String directoryPath { sandboxDirectory(parameters.processType, dataVaultParentDirectory) };
    String filePath = sandboxFilePath(directoryPath);
    SandboxInfo info {
        dataVaultParentDirectory,
        directoryPath,
        filePath,
        sandboxParameters,
        *header,
        parameters.processType,
        sandboxInitializationParameters,
        profileOrProfilePath,
        isProfilePath
    };

    if (tryApplyCachedSandbox(info))
        return true;

    SandboxProfilePtr sandboxProfile = compileAndCacheSandboxProfile(info);
    if (!sandboxProfile)
        return compileAndApplySandboxSlowCase(profileOrProfilePath, isProfilePath, sandboxInitializationParameters);

    if (sandbox_apply(sandboxProfile.get())) {
        WTFLogAlways("%s: Could not apply compiled sandbox: %s\n", getprogname(), safeStrerror(errno).data());
        CRASH();
    }

    return true;
#else
    UNUSED_PARAM(parameters);
    UNUSED_PARAM(dataVaultParentDirectory);
    return compileAndApplySandboxSlowCase(profileOrProfilePath, isProfilePath, sandboxInitializationParameters);
#endif // USE(CACHE_COMPILED_SANDBOX)
}

static String getUserDirectorySuffix(const AuxiliaryProcessInitializationParameters& parameters)
{
    auto userDirectorySuffix = parameters.extraInitializationData.find<HashTranslatorASCIILiteral>("user-directory-suffix"_s);
    if (userDirectorySuffix != parameters.extraInitializationData.end()) {
        String suffix = userDirectorySuffix->value;
        return suffix.left(suffix.find('/'));
    }

    String clientIdentifier = codeSigningIdentifier(parameters.connectionIdentifier.xpcConnection.get());
    if (clientIdentifier.isNull())
        clientIdentifier = parameters.clientIdentifier;
    return makeString([[NSBundle mainBundle] bundleIdentifier], '+', clientIdentifier);
}

static StringView parseOSVersion(StringView osSystemMarketingVersion)
{
    auto firstDotIndex = osSystemMarketingVersion.find('.');
    if (firstDotIndex == notFound)
        return { };
    auto secondDotIndex = osSystemMarketingVersion.find('.', firstDotIndex + 1);
    if (secondDotIndex == notFound)
        return osSystemMarketingVersion;
    return osSystemMarketingVersion.left(secondDotIndex);
}

static String getHomeDirectory()
{
    // According to the man page for getpwuid_r, we should use sysconf(_SC_GETPW_R_SIZE_MAX) to determine the size of the buffer.
    // However, a buffer size of 4096 should be sufficient, since PATH_MAX is 1024.
    char buffer[4096];
    passwd pwd;
    passwd* result = nullptr;
    if (getpwuid_r(getuid(), &pwd, buffer, sizeof(buffer), &result) || !result) {
        WTFLogAlways("%s: Couldn't find home directory", getprogname());
        RELEASE_ASSERT_NOT_REACHED();
    }
    return String::fromUTF8(pwd.pw_dir);
}

static void closeOpenDirectoryConnections()
{
    if (mbr_close_connectionsPtr())
        mbr_close_connectionsPtr()();
    if (lookup_close_connectionsPtr())
        lookup_close_connectionsPtr()();
}

static void populateSandboxInitializationParameters(SandboxInitializationParameters& sandboxParameters)
{
    RELEASE_ASSERT(!sandboxParameters.userDirectorySuffix().isNull());

    String osSystemMarketingVersion = systemMarketingVersion();
    auto osVersion = parseOSVersion(osSystemMarketingVersion);
    if (osVersion.isNull()) {
        WTFLogAlways("%s: Couldn't find OS Version\n", getprogname());
        exitProcess(EX_NOPERM);
    }
    sandboxParameters.addParameter("_OS_VERSION"_s, osVersion.utf8());

    // Use private temporary and cache directories.
    setenv("DIRHELPER_USER_DIR_SUFFIX", FileSystem::fileSystemRepresentation(sandboxParameters.userDirectorySuffix()).data(), 1);
    char temporaryDirectory[PATH_MAX];
    if (!confstr(_CS_DARWIN_USER_TEMP_DIR, temporaryDirectory, sizeof(temporaryDirectory))) {
        WTFLogAlways("%s: couldn't retrieve private temporary directory path: %d\n", getprogname(), errno);
        exitProcess(EX_NOPERM);
    }
    setenv("TMPDIR", temporaryDirectory, 1);

    String bundlePath = webKit2Bundle().bundlePath;
    if (!bundlePath.startsWith("/System/Library/Frameworks"_s))
        bundlePath = webKit2Bundle().bundlePath.stringByDeletingLastPathComponent;

    sandboxParameters.addPathParameter("WEBKIT2_FRAMEWORK_DIR"_s, bundlePath.utf8().data());
    sandboxParameters.addConfDirectoryParameter("DARWIN_USER_TEMP_DIR"_s, _CS_DARWIN_USER_TEMP_DIR);
    sandboxParameters.addConfDirectoryParameter("DARWIN_USER_CACHE_DIR"_s, _CS_DARWIN_USER_CACHE_DIR);

    auto homeDirectory = getHomeDirectory();
    
    sandboxParameters.addPathParameter("HOME_DIR"_s, homeDirectory.utf8().data());
    String path = FileSystem::pathByAppendingComponent(homeDirectory, "Library"_s);
    sandboxParameters.addPathParameter("HOME_LIBRARY_DIR"_s, FileSystem::fileSystemRepresentation(path).data());
    path = FileSystem::pathByAppendingComponent(path, "/Preferences"_s);
    sandboxParameters.addPathParameter("HOME_LIBRARY_PREFERENCES_DIR"_s, FileSystem::fileSystemRepresentation(path).data());

#if CPU(X86_64)
    sandboxParameters.addParameter("CPU"_s, "x86_64"_span);
#elif CPU(ARM64)
    sandboxParameters.addParameter("CPU"_s, "arm64"_span);
#else
#error "Unknown architecture."
#endif

    closeOpenDirectoryConnections();

    if (HIS_XPC_ResetMessageConnectionPtr())
        HIS_XPC_ResetMessageConnectionPtr()();
}

void AuxiliaryProcess::initializeSandbox(const AuxiliaryProcessInitializationParameters& parameters, SandboxInitializationParameters& sandboxParameters)
{
    TraceScope traceScope(InitializeSandboxStart, InitializeSandboxEnd);

#if USE(CACHE_COMPILED_SANDBOX)
    // This must be called before populateSandboxInitializationParameters so that the path does not include the user directory suffix.
    // We don't want the user directory suffix because we want all processes of the same type to use the same cache directory.
    String dataVaultParentDirectory { sandboxDataVaultParentDirectory() };
#else
    String dataVaultParentDirectory;
#endif

    bool enableMessageFilter = false;
#if HAVE(SANDBOX_MESSAGE_FILTERING)
    enableMessageFilter = WTF::processHasEntitlement("com.apple.private.security.message-filter"_s);
#endif
    sandboxParameters.addParameter("ENABLE_SANDBOX_MESSAGE_FILTER"_s, enableMessageFilter ? "YES"_span : "NO"_span);

    if (sandboxParameters.userDirectorySuffix().isNull())
        sandboxParameters.setUserDirectorySuffix(getUserDirectorySuffix(parameters));

    populateSandboxInitializationParameters(sandboxParameters);

    if (!applySandbox(parameters, sandboxParameters, dataVaultParentDirectory)) {
        WTFLogAlways("%s: Unable to apply sandbox\n", getprogname());
        CRASH();
    }

    if (shouldOverrideQuarantine()) {
        // This will override LSFileQuarantineEnabled from Info.plist unless sandbox quarantine is globally disabled.
        OSStatus error = enableSandboxStyleFileQuarantine();
        if (error) {
            WTFLogAlways("%s: Couldn't enable sandbox style file quarantine: %ld\n", getprogname(), static_cast<long>(error));
            exitProcess(EX_NOPERM);
        }
    }
}

void AuxiliaryProcess::applySandboxProfileForDaemon(const String& profilePath, const String& userDirectorySuffix)
{
    TraceScope traceScope(InitializeSandboxStart, InitializeSandboxEnd);

    SandboxInitializationParameters parameters { };
    parameters.setOverrideSandboxProfilePath(profilePath);
    parameters.setUserDirectorySuffix(userDirectorySuffix);
    populateSandboxInitializationParameters(parameters);

    String profileOrProfilePath;
    bool isProfilePath;
    getSandboxProfileOrProfilePath(parameters, profileOrProfilePath, isProfilePath);
    RELEASE_ASSERT(!profileOrProfilePath.isEmpty());

    bool success = compileAndApplySandboxSlowCase(profileOrProfilePath, isProfilePath, parameters);
    RELEASE_ASSERT(success);
}

#if USE(APPKIT)
void AuxiliaryProcess::stopNSAppRunLoop()
{
    ASSERT([NSApp isRunning]);
    [NSApp stop:nil];

    RetainPtr event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSMakePoint(0, 0) modifierFlags:0 timestamp:0.0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    [NSApp postEvent:event.get() atStart:true];
}
#endif

#if !PLATFORM(MACCATALYST) && ENABLE(WEBPROCESS_NSRUNLOOP)
void AuxiliaryProcess::stopNSRunLoop()
{
    ASSERT([NSRunLoop mainRunLoop]);
    [[NSRunLoop mainRunLoop] performBlock:^{
        exitProcess(0);
    }];
}
#endif

void AuxiliaryProcess::setQOS(int latencyQOS, int throughputQOS)
{
    if (!latencyQOS && !throughputQOS)
        return;

    struct task_qos_policy qosinfo = {
        latencyQOS ? LATENCY_QOS_TIER_0 + latencyQOS - 1 : LATENCY_QOS_TIER_UNSPECIFIED,
        throughputQOS ? THROUGHPUT_QOS_TIER_0 + throughputQOS - 1 : THROUGHPUT_QOS_TIER_UNSPECIFIED
    };

    task_policy_set(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, TASK_QOS_POLICY_COUNT);
}

#if PLATFORM(MAC)
void AuxiliaryProcess::openDirectoryCacheInvalidated(SandboxExtension::Handle&& handle)
{
    // When Open Directory has invalidated the in-process cache for the results of getpwnam/getpwuid_r,
    // we need to rebuild the cache by getting the home directory while holding a temporary sandbox
    // extension to the associated Open Directory service.

    auto sandboxExtension = SandboxExtension::create(WTFMove(handle));
    if (!sandboxExtension)
        return;

    sandboxExtension->consume();

    getHomeDirectory();

    closeOpenDirectoryConnections();

    sandboxExtension->revoke();
}
#endif // PLATFORM(MAC)

} // namespace WebKit

#endif
