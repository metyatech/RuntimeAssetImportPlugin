// Copyright (c) 2026 metyatech. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IImageWrapper.h"

namespace RuntimeAssetImport::CompressedTextureValidation
{
    struct FCompressedTextureMetadata
    {
        EImageFormat Format = EImageFormat::Invalid;
        int64 Width = 0;
        int64 Height = 0;
    };

    RUNTIMEASSETIMPORT_API bool ValidateCompressedTexturePayload(const void *Data, int64 ByteCount,
                                                                 FCompressedTextureMetadata &OutMetadata,
                                                                 FString &OutErrorMessage);

    RUNTIMEASSETIMPORT_API bool ValidateCompressedTexturePayload(const TArray<uint8> &Data,
                                                                 FCompressedTextureMetadata &OutMetadata,
                                                                 FString &OutErrorMessage);
} // namespace RuntimeAssetImport::CompressedTextureValidation
