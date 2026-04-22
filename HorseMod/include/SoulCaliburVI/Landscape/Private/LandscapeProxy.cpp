#include "LandscapeProxy.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SceneComponent -FallbackName=SceneComponent

ALandscapeProxy::ALandscapeProxy(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAllowTickBeforeBeginPlay = true;
    this->bCanBeDamaged = false;
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
    this->SplineComponent = NULL;
    this->MaxLODLevel = -1;
    this->LODDistanceFactor = 1.00f;
    this->LODFalloff = ELandscapeLODFalloff::Linear;
    this->StaticLightingLOD = 0;
    this->DefaultPhysMaterial = NULL;
    this->StreamingDistanceMultiplier = 1.00f;
    this->LandscapeMaterial = NULL;
    this->LandscapeHoleMaterial = NULL;
    this->NegativeZBoundsExtension = 0.00f;
    this->PositiveZBoundsExtension = 0.00f;
    this->bHasLandscapeGrass = true;
    this->StaticLightingResolution = 1.00f;
    this->bCastStaticShadow = true;
    this->bCastShadowAsTwoSided = false;
    this->bCastFarShadow = true;
    this->bUseMaterialPositionOffsetInStaticLighting = false;
    this->bRenderCustomDepth = false;
    this->CustomDepthStencilValue = 0;
    this->CollisionMipLevel = 0;
    this->SimpleCollisionMipLevel = 0;
    this->CollisionThickness = 16.00f;
    this->bGenerateOverlapEvents = false;
    this->bBakeMaterialPositionOffsetIntoCollision = false;
    this->ComponentSizeQuads = 0;
    this->SubsectionSizeQuads = 0;
    this->NumSubsections = 0;
    this->bUsedForNavigation = true;
    this->NavigationGeometryGatheringMode = ENavDataGatheringMode::Default;
    this->bUseLandscapeForCullingInvisibleHLODVertices = false;
}

void ALandscapeProxy::EditorApplySpline(USplineComponent* InSplineComponent, float StartWidth, float EndWidth, float StartSideFalloff, float EndSideFalloff, float StartRoll, float EndRoll, int32 NumSubdivisions, bool bRaiseHeights, bool bLowerHeights, ULandscapeLayerInfoObject* PaintLayer) {
}

void ALandscapeProxy::ChangeLODDistanceFactor(float InLODDistanceFactor) {
}


