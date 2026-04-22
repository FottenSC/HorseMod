#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "MovieSceneBindingOverrideData.h"
#include "MovieSceneBindingOverridesInterface.h"
#include "MovieSceneBindingOverrides.generated.h"

UCLASS(Blueprintable, DefaultToInstanced, EditInlineNew)
class MOVIESCENE_API UMovieSceneBindingOverrides : public UObject, public IMovieSceneBindingOverridesInterface {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FMovieSceneBindingOverrideData> BindingData;
    
public:
    UMovieSceneBindingOverrides();


    // Fix for true pure virtual functions not being implemented
};

