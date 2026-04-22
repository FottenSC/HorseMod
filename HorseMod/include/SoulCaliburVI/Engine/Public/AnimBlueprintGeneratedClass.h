#pragma once
#include "CoreMinimal.h"
#include "AnimClassInterface.h"
#include "AnimNotifyEvent.h"
#include "BakedAnimationStateMachine.h"
#include "BlueprintGeneratedClass.h"
#include "AnimBlueprintGeneratedClass.generated.h"

class USkeleton;

UCLASS(Blueprintable)
class ENGINE_API UAnimBlueprintGeneratedClass : public UBlueprintGeneratedClass, public IAnimClassInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FBakedAnimationStateMachine> BakedStateMachines;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USkeleton* TargetSkeleton;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FAnimNotifyEvent> AnimNotifies;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 RootAnimNodeIndex;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<int32> OrderedSavedPoseIndices;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FName> SyncGroupNames;
    
    UAnimBlueprintGeneratedClass();


    // Fix for true pure virtual functions not being implemented
};

