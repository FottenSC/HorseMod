#pragma once
#include "CoreMinimal.h"
#include "MovieSceneAdditiveCameraAnimationTemplate.h"
#include "MovieSceneCameraAnimSectionData.h"
#include "MovieSceneCameraAnimSectionTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneCameraAnimSectionTemplate : public FMovieSceneAdditiveCameraAnimationTemplate {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneCameraAnimSectionData SourceData;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float SectionStartTime;
    
public:
    MOVIESCENETRACKS_API FMovieSceneCameraAnimSectionTemplate();
};

