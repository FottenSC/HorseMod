#pragma once
#include "CoreMinimal.h"
#include "MovieSceneCaptureProtocolSettings.h"
#include "BmpImageCaptureSettings.generated.h"

UCLASS(Blueprintable, Config=EditorPerProjectUserSettings)
class MOVIESCENECAPTURE_API UBmpImageCaptureSettings : public UMovieSceneCaptureProtocolSettings {
    GENERATED_BODY()
public:
    UBmpImageCaptureSettings();

};

