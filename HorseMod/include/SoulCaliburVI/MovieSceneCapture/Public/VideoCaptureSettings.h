#pragma once
#include "CoreMinimal.h"
#include "FrameGrabberProtocolSettings.h"
#include "VideoCaptureSettings.generated.h"

UCLASS(Blueprintable, Config=EditorPerProjectUserSettings)
class MOVIESCENECAPTURE_API UVideoCaptureSettings : public UFrameGrabberProtocolSettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bUseCompression;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CompressionQuality;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString VideoCodec;
    
    UVideoCaptureSettings();

};

