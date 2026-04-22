#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneEvalTemplate -FallbackName=MovieSceneEvalTemplate
#include "MovieSceneAdditiveCameraAnimationTrackTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneAdditiveCameraAnimationTrackTemplate : public FMovieSceneEvalTemplate {
    GENERATED_BODY()
public:
    MOVIESCENETRACKS_API FMovieSceneAdditiveCameraAnimationTrackTemplate();
};

