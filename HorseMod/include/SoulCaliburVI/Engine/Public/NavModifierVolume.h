#pragma once
#include "CoreMinimal.h"
#include "NavRelevantInterface.h"
#include "Templates/SubclassOf.h"
#include "Volume.h"
#include "NavModifierVolume.generated.h"

class UNavArea;

UCLASS(Blueprintable)
class ENGINE_API ANavModifierVolume : public AVolume, public INavRelevantInterface {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TSubclassOf<UNavArea> AreaClass;
    
public:
    ANavModifierVolume(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SetAreaClass(TSubclassOf<UNavArea> NewAreaClass);
    

    // Fix for true pure virtual functions not being implemented
};

