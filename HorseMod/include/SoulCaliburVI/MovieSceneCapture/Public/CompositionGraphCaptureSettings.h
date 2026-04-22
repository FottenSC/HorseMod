#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
#include "CompositionGraphCapturePasses.h"
#include "EHDRCaptureGamut.h"
#include "MovieSceneCaptureProtocolSettings.h"
#include "CompositionGraphCaptureSettings.generated.h"

UCLASS(Blueprintable, Config=EditorPerProjectUserSettings)
class MOVIESCENECAPTURE_API UCompositionGraphCaptureSettings : public UMovieSceneCaptureProtocolSettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    FCompositionGraphCapturePasses IncludeRenderPasses;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bCaptureFramesInHDR;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 HDRCompressionQuality;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EHDRCaptureGamut> CaptureGamut;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStringAssetReference PostProcessingMaterial;
    
    UCompositionGraphCaptureSettings();

};

