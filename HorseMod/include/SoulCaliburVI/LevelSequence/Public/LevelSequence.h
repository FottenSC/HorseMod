#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneSequence -FallbackName=MovieSceneSequence
#include "LevelSequenceBindingReferences.h"
#include "LevelSequenceObject.h"
#include "LevelSequenceObjectReferenceMap.h"
#include "LevelSequence.generated.h"

class UMovieScene;

UCLASS(Blueprintable)
class LEVELSEQUENCE_API ULevelSequence : public UMovieSceneSequence {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UMovieScene* MovieScene;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLevelSequenceObjectReferenceMap ObjectReferences;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLevelSequenceBindingReferences BindingReferences;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FString, FLevelSequenceObject> PossessedObjects;
    
public:
    ULevelSequence();

};

