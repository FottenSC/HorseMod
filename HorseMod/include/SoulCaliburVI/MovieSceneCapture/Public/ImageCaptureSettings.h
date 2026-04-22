#pragma once
#include "CoreMinimal.h"
#include "FrameGrabberProtocolSettings.h"
#include "ImageCaptureSettings.generated.h"

UCLASS(Blueprintable, Config=EditorPerProjectUserSettings)
class MOVIESCENECAPTURE_API UImageCaptureSettings : public UFrameGrabberProtocolSettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 CompressionQuality;
    
    UImageCaptureSettings();

};

