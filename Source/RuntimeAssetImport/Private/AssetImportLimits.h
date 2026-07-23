// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace RuntimeAssetImport::Limits
{
    constexpr uint64 MaximumMainModelFileBytes = 512ull * 1024ull * 1024ull;
    constexpr uint64 MaximumAuxiliaryFileBytes = 256ull * 1024ull * 1024ull;
    constexpr uint64 MaximumCompressedTextureBytes = 64ull * 1024ull * 1024ull;
    constexpr uint64 MaximumTotalUniqueOpenedBytes = 1024ull * 1024ull * 1024ull;
    constexpr int32 MaximumUniqueOpenedFiles = 256;
    constexpr uint32 MaximumRawTextureDimension = 16384;
    constexpr uint64 MaximumRawTexturePixels = 67108864ull;

    constexpr bool IsCompressedTextureByteCountValid(const uint64 ByteCount)
    {
        return ByteCount > 0 && ByteCount <= MaximumCompressedTextureBytes;
    }
} // namespace RuntimeAssetImport::Limits
