#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "Actor.h"
#include "NavLinkHostInterface.h"
#include "NavRelevantInterface.h"
#include "NavigationLink.h"
#include "NavigationSegmentLink.h"
#include "SmartLinkReachedSignatureDelegate.h"
#include "NavLinkProxy.generated.h"

class UNavLinkCustomComponent;

UCLASS(Blueprintable)
class ENGINE_API ANavLinkProxy : public AActor, public INavLinkHostInterface, public INavRelevantInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNavigationLink> PointLinks;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNavigationSegmentLink> SegmentLinks;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UNavLinkCustomComponent* SmartLinkComp;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bSmartLinkIsRelevant;
    
protected:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FSmartLinkReachedSignature OnSmartLinkReached;
    
public:
    ANavLinkProxy(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SetSmartLinkEnabled(bool bEnabled);
    
    UFUNCTION(BlueprintCallable)
    void ResumePathFollowing(AActor* Agent);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveSmartLinkReached(AActor* Agent, const FVector& Destination);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsSmartLinkEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool HasMovingAgents() const;
    

    // Fix for true pure virtual functions not being implemented
};

