#pragma once
#include "CoreMinimal.h"
#include "MovieSceneEvalTemplate.h"
#include "MovieSceneLegacyTrackInstanceTemplate.generated.h"

class UMovieSceneTrack;

USTRUCT(BlueprintType)
struct MOVIESCENE_API FMovieSceneLegacyTrackInstanceTemplate : public FMovieSceneEvalTemplate {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UMovieSceneTrack* track;
    
    FMovieSceneLegacyTrackInstanceTemplate();
};

