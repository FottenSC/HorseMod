#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=IntegralCurve -FallbackName=IntegralCurve
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieScenePropertySectionTemplate -FallbackName=MovieScenePropertySectionTemplate
#include "MovieSceneEnumPropertySectionTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneEnumPropertySectionTemplate : public FMovieScenePropertySectionTemplate {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FIntegralCurve EnumCurve;
    
public:
    MOVIESCENETRACKS_API FMovieSceneEnumPropertySectionTemplate();
};

