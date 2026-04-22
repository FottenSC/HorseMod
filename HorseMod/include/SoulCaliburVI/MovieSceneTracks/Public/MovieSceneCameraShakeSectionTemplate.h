#pragma once
#include "CoreMinimal.h"
#include "MovieSceneAdditiveCameraAnimationTemplate.h"
#include "MovieSceneCameraShakeSectionData.h"
#include "MovieSceneCameraShakeSectionTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneCameraShakeSectionTemplate : public FMovieSceneAdditiveCameraAnimationTemplate {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneCameraShakeSectionData SourceData;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float SectionStartTime;
    
public:
    MOVIESCENETRACKS_API FMovieSceneCameraShakeSectionTemplate();
};

