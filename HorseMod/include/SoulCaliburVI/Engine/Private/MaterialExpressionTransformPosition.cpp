#include "MaterialExpressionTransformPosition.h"

UMaterialExpressionTransformPosition::UMaterialExpressionTransformPosition() {
    this->Outputs.AddDefaulted(1);
    this->TransformSourceType = TRANSFORMPOSSOURCE_Local;
    this->TransformType = TRANSFORMPOSSOURCE_Local;
}


