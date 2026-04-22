#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StringCurve -FallbackName=StringCurve
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieScenePropertySectionTemplate -FallbackName=MovieScenePropertySectionTemplate
#include "MovieSceneStringPropertySectionTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneStringPropertySectionTemplate : public FMovieScenePropertySectionTemplate {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStringCurve StringCurve;
    
public:
    MOVIESCENETRACKS_API FMovieSceneStringPropertySectionTemplate();
};

