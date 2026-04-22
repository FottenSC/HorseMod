#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieSceneTracks -ObjectName=MovieSceneParameterSectionTemplate -FallbackName=MovieSceneParameterSectionTemplate
#include "MovieSceneRetainerBoxTrackTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneRetainerBoxTrackTemplate : public FMovieSceneParameterSectionTemplate {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 MaterialIndex;
    
public:
    UMGUTIL_API FMovieSceneRetainerBoxTrackTemplate();
};

