#pragma once
#include "CoreMinimal.h"
#include "MovieSceneSkeletalAnimationParams.h"
#include "MovieSceneSkeletalAnimationSectionTemplateParameters.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneSkeletalAnimationSectionTemplateParameters : public FMovieSceneSkeletalAnimationParams {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float SectionStartTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float SectionEndTime;
    
    MOVIESCENETRACKS_API FMovieSceneSkeletalAnimationSectionTemplateParameters();
};

