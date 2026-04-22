#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneEvalTemplate -FallbackName=MovieSceneEvalTemplate
#include "MovieSceneAudioSectionTemplateData.h"
#include "MovieSceneAudioSectionTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneAudioSectionTemplate : public FMovieSceneEvalTemplate {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneAudioSectionTemplateData AudioData;
    
    MOVIESCENETRACKS_API FMovieSceneAudioSectionTemplate();
};

