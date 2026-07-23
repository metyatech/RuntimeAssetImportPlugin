// Copyright (c) 2026 metyatech. All rights reserved.

#include "CompressedTextureValidation.h"

#include "AssetImportLimits.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

namespace RuntimeAssetImport::CompressedTextureValidation
{
    namespace
    {
        FString GetFormatName(IImageWrapperModule &ImageWrapperModule, const EImageFormat Format)
        {
            if (Format == EImageFormat::Invalid)
            {
                return TEXT("Invalid");
            }

            const TCHAR *Extension = ImageWrapperModule.GetExtension(Format);
            return Extension != nullptr && Extension[0] != TEXT('\0')
                       ? FString(Extension).ToUpper()
                       : FString::Printf(TEXT("enum-%d"), static_cast<int32>(Format));
        }
    } // namespace

    bool ValidateCompressedTexturePayload(const void *Data, const int64 ByteCount,
                                          FCompressedTextureMetadata &OutMetadata, FString &OutErrorMessage)
    {
        OutMetadata = FCompressedTextureMetadata();
        OutErrorMessage.Reset();
        if (Data == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: data=null, compressed-bytes=%lld."), ByteCount);
            return false;
        }
        if (ByteCount <= 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: compressed-bytes=%lld; minimum=1."), ByteCount);
            return false;
        }
        if (static_cast<uint64>(ByteCount) > Limits::MaximumCompressedTextureBytes)
        {
            OutErrorMessage = FString::Printf(TEXT("Compressed texture metadata validation failed: "
                                                   "compressed-bytes=%lld; compressed-byte-limit=%llu."),
                                              ByteCount, Limits::MaximumCompressedTextureBytes);
            return false;
        }
        if (ByteCount > MAX_int32)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: compressed-bytes=%lld; int32-limit=%d."),
                ByteCount, MAX_int32);
            return false;
        }

        IImageWrapperModule *ImageWrapperModule =
            FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
        if (ImageWrapperModule == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: format=unknown, compressed-bytes=%lld; "
                     "ImageWrapper module is unavailable."),
                ByteCount);
            return false;
        }

        OutMetadata.Format = ImageWrapperModule->DetectImageFormat(Data, ByteCount);
        const FString FormatName = GetFormatName(*ImageWrapperModule, OutMetadata.Format);
        if (OutMetadata.Format == EImageFormat::Invalid)
        {
            OutErrorMessage =
                FString::Printf(TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld; "
                                     "the image format was not detected."),
                                *FormatName, ByteCount);
            return false;
        }

        const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(OutMetadata.Format);
        if (!ImageWrapper.IsValid())
        {
            OutErrorMessage =
                FString::Printf(TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld; "
                                     "the format wrapper is unavailable."),
                                *FormatName, ByteCount);
            return false;
        }
        if (!ImageWrapper->SetCompressed(Data, ByteCount))
        {
            OutErrorMessage =
                FString::Printf(TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld; "
                                     "the compressed header or payload is invalid."),
                                *FormatName, ByteCount);
            return false;
        }

        OutMetadata.Width = ImageWrapper->GetWidth();
        OutMetadata.Height = ImageWrapper->GetHeight();
        if (OutMetadata.Width < 1 || OutMetadata.Height < 1)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld, width=%lld, "
                     "height=%lld, pixel-count=not-evaluated; minimum-dimension=1."),
                *FormatName, ByteCount, OutMetadata.Width, OutMetadata.Height);
            return false;
        }
        if (static_cast<uint64>(OutMetadata.Width) > Limits::MaximumRawTextureDimension ||
            static_cast<uint64>(OutMetadata.Height) > Limits::MaximumRawTextureDimension)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld, width=%lld, "
                     "height=%lld, pixel-count=not-evaluated; dimension-limit=%u."),
                *FormatName, ByteCount, OutMetadata.Width, OutMetadata.Height, Limits::MaximumRawTextureDimension);
            return false;
        }

        const uint64 Width = static_cast<uint64>(OutMetadata.Width);
        const uint64 Height = static_cast<uint64>(OutMetadata.Height);
        if (Height > MAX_uint64 / Width)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld, width=%lld, "
                     "height=%lld, pixel-count=overflow; pixel-limit=%llu."),
                *FormatName, ByteCount, OutMetadata.Width, OutMetadata.Height, Limits::MaximumRawTexturePixels);
            return false;
        }

        const uint64 PixelCount = Width * Height;
        if (PixelCount > Limits::MaximumRawTexturePixels)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Compressed texture metadata validation failed: format=%s, compressed-bytes=%lld, width=%lld, "
                     "height=%lld, pixel-count=%llu; pixel-limit=%llu."),
                *FormatName, ByteCount, OutMetadata.Width, OutMetadata.Height, PixelCount,
                Limits::MaximumRawTexturePixels);
            return false;
        }
        return true;
    }

    bool ValidateCompressedTexturePayload(const TArray<uint8> &Data, FCompressedTextureMetadata &OutMetadata,
                                          FString &OutErrorMessage)
    {
        return ValidateCompressedTexturePayload(Data.GetData(), static_cast<int64>(Data.Num()), OutMetadata,
                                                OutErrorMessage);
    }
} // namespace RuntimeAssetImport::CompressedTextureValidation
