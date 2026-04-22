#pragma once
#include "CoreMinimal.h"
#include "NavLinkHostInterface.h"
#include "NavigationLink.h"
#include "PrimitiveComponent.h"
#include "NavLinkComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UNavLinkComponent : public UPrimitiveComponent, public INavLinkHostInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNavigationLink> Links;
    
    UNavLinkComponent(const FObjectInitializer& ObjectInitializer);


    // Fix for true pure virtual functions not being implemented
};

