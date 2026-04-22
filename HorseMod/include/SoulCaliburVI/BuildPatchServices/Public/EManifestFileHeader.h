#pragma once
#include "CoreMinimal.h"
#include "EManifestFileHeader.generated.h"

UENUM(BlueprintType)
namespace EManifestFileHeader {
    enum Type {
        STORED_RAW,
        STORED_COMPRESSED,
    };
}

