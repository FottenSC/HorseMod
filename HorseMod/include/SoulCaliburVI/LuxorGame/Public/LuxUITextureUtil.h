#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Color -FallbackName=Color
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataObject -FallbackName=UIDataObject
#include "ELuxorCompressedImageFormat.h"
#include "LuxUITextureParam.h"
#include "LuxUITextureUtil.generated.h"

class UMaterialInstanceDynamic;
class UObject;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUITextureUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUITextureUtil();

    UFUNCTION(BlueprintCallable)
    static void ResizeImageWithParam(const FLuxUITextureParam& InParam, int32 NewSizeX, int32 NewSizeY, FLuxUITextureParam& outParam);
    
    UFUNCTION(BlueprintCallable)
    static bool ResizeImage(TArray<FColor>& OutColors, const TArray<FColor>& InColors, int32 OriginalWidth, int32 OriginalHeight, int32 NewWidth, int32 NewHeight);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsValidForLuxUITextureParam(const FLuxUITextureParam& InParam);
    
    UFUNCTION(BlueprintCallable)
    static void DownscaleImageFromParam(const FLuxUITextureParam& InParam, int32 DownScaleFactor, FLuxUITextureParam& NewParam);
    
    UFUNCTION(BlueprintCallable)
    static void DownscaleImage(TArray<FColor>& OutColors, const TArray<FColor>& InColors, int32 OriginalWidth, int32 OriginalHeight, int32 DownScaleFactor);
    
    UFUNCTION(BlueprintCallable)
    static UMaterialInstanceDynamic* CreateThumbnailByIds(UTexture2D* InThumbnail, int32 InBackGround, int32 InBackFrame, int32 InFrontFrame);
    
    UFUNCTION(BlueprintCallable)
    static UMaterialInstanceDynamic* CreateThumbnail(UTexture2D* InThumbnail, UTexture2D* InBackGround, UTexture2D* InBackFrame, UTexture2D* InFrontFrame);
    
    UFUNCTION(BlueprintCallable)
    static FLuxUITextureParam CreateTextureParamFromUIDataObject(FUIDataObject InObject);
    
    UFUNCTION(BlueprintCallable)
    static FLuxUITextureParam CreateTextureParamFromTexture2D(UTexture2D* inTexture);
    
    UFUNCTION(BlueprintCallable)
    static FLuxUITextureParam CreateTextureParamFromRenderTarget2DApplyUVInfo(UTextureRenderTarget2D* inTexture, const FVector2D& InStartUV, const FVector2D& InEndUV);
    
    UFUNCTION(BlueprintCallable)
    static FLuxUITextureParam CreateTextureParamFromRenderTarget2D(UTextureRenderTarget2D* inTexture);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static UTexture* CreateTextureFromStringAsset(const FStringAssetReference& AssetReference);
    
    UFUNCTION(BlueprintCallable)
    static UTexture2D* CreateTexture2DFromRenderTarget2D(UTextureRenderTarget2D* inTexture);
    
    UFUNCTION(BlueprintCallable)
    static UTexture2D* CreateTexture2DFromParam(const FLuxUITextureParam& InParam);
    
    UFUNCTION(BlueprintCallable)
    static UTexture2D* CreateTexture2DFromCompressedImage(ELuxorCompressedImageFormat ImageFormat, const TArray<uint8>& ImageCompressedData);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static UTextureRenderTarget2D* CreateSizeAdjustedRenderTargetTexture2DWithBaseResolutionFullHD(UObject* WorldContext, int32 Width, int32 Height);
    
    UFUNCTION(BlueprintCallable)
    static UMaterialInstanceDynamic* CreateBannedSafeThumbnail();
    
};

