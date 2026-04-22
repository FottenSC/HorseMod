#pragma once
#include "CoreMinimal.h"
#include "MovieSceneEvaluationTemplate.h"
#include "CachedMovieSceneEvaluationTemplate.generated.h"

USTRUCT(BlueprintType)
struct FCachedMovieSceneEvaluationTemplate : public FMovieSceneEvaluationTemplate {
    GENERATED_BODY()
public:
    MOVIESCENE_API FCachedMovieSceneEvaluationTemplate();
};

