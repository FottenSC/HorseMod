#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "BlendableInterface.h"
#include "LightmassMaterialInterfaceSettings.h"
#include "MaterialTextureInfo.h"
#include "MaterialInterface.generated.h"

class UMaterial;
class UPhysicalMaterial;
class USubsurfaceProfile;

UCLASS(Abstract, Blueprintable, MinimalAPI)
class UMaterialInterface : public UObject, public IBlendableInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USubsurfaceProfile* SubsurfaceProfile;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLightmassMaterialInterfaceSettings LightmassSettings;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FMaterialTextureInfo> TextureStreamingData;
    
public:
    UMaterialInterface();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    UPhysicalMaterial* GetPhysicalMaterial() const;
    
    UFUNCTION(BlueprintCallable)
    UMaterial* GetBaseMaterial();
    

    // Fix for true pure virtual functions not being implemented
};

