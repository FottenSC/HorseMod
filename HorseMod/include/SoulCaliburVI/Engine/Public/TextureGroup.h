#pragma once
#include "CoreMinimal.h"
#include "TextureGroup.generated.h"

UENUM(BlueprintType)
enum TextureGroup {
    TEXTUREGROUP_World,
    TEXTUREGROUP_WorldNormalMap,
    TEXTUREGROUP_WorldSpecular,
    TEXTUREGROUP_World_Low,
    TEXTUREGROUP_WorldNormalMap_Low,
    TEXTUREGROUP_WorldSpecular_Low,
    TEXTUREGROUP_Character,
    TEXTUREGROUP_CharacterNormalMap,
    TEXTUREGROUP_CharacterSpecular,
    TEXTUREGROUP_Weapon,
    TEXTUREGROUP_WeaponNormalMap,
    TEXTUREGROUP_WeaponSpecular,
    TEXTUREGROUP_Vehicle,
    TEXTUREGROUP_VehicleNormalMap,
    TEXTUREGROUP_VehicleSpecular,
    TEXTUREGROUP_Cinematic,
    TEXTUREGROUP_Effects,
    TEXTUREGROUP_EffectsNotFiltered,
    TEXTUREGROUP_Skybox,
    TEXTUREGROUP_UI,
    TEXTUREGROUP_Lightmap,
    TEXTUREGROUP_RenderTarget,
    TEXTUREGROUP_MobileFlattened,
    TEXTUREGROUP_ProcBuilding_Face,
    TEXTUREGROUP_ProcBuilding_LightMap,
    TEXTUREGROUP_Shadowmap,
    TEXTUREGROUP_ColorLookupTable,
    TEXTUREGROUP_Terrain_Heightmap,
    TEXTUREGROUP_Terrain_Weightmap,
    TEXTUREGROUP_Bokeh,
    TEXTUREGROUP_IESLightProfile,
    TEXTUREGROUP_Pixels2D,
    TEXTUREGROUP_HierarchicalLOD,
};

