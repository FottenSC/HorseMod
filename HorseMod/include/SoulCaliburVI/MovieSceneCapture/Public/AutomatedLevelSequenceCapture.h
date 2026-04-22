#pragma once
#include "CoreMinimal.h"
#include "MovieSceneCapture.h"
#include "AutomatedLevelSequenceCapture.generated.h"

UCLASS(Blueprintable, Config=EditorSettings)
class MOVIESCENECAPTURE_API UAutomatedLevelSequenceCapture : public UMovieSceneCapture {
    GENERATED_BODY()
public:
    UAutomatedLevelSequenceCapture();

};

