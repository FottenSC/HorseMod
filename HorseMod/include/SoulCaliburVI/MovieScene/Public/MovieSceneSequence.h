#pragma once
#include "CoreMinimal.h"
#include "CachedMovieSceneEvaluationTemplate.h"
#include "MovieSceneSignedObject.h"
#include "MovieSceneTrackCompilationParams.h"
#include "MovieSceneSequence.generated.h"

class UObject;

UCLASS(Blueprintable, MinimalAPI)
class UMovieSceneSequence : public UMovieSceneSignedObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FCachedMovieSceneEvaluationTemplate EvaluationTemplate;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneTrackCompilationParams TemplateParameters;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<UObject*, FCachedMovieSceneEvaluationTemplate> InstancedSubSequenceEvaluationTemplates;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bParentContextsAreSignificant;
    
public:
    UMovieSceneSequence();

};

