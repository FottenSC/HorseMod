#pragma once
#include "CoreMinimal.h"
#include "MovieSceneCaptureProtocolSettings.h"
#include "FrameGrabberProtocolSettings.generated.h"

UCLASS(Blueprintable)
class MOVIESCENECAPTURE_API UFrameGrabberProtocolSettings : public UMovieSceneCaptureProtocolSettings {
    GENERATED_BODY()
public:
    UFrameGrabberProtocolSettings();

};

