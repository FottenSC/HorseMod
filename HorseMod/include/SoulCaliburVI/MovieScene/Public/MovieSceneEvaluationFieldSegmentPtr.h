#pragma once
#include "CoreMinimal.h"
#include "MovieSceneEvaluationFieldTrackPtr.h"
#include "MovieSceneEvaluationFieldSegmentPtr.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneEvaluationFieldSegmentPtr : public FMovieSceneEvaluationFieldTrackPtr {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 SegmentIndex;
    
    MOVIESCENE_API FMovieSceneEvaluationFieldSegmentPtr();
};

