#include "MaterialExpressionTransform.h"

UMaterialExpressionTransform::UMaterialExpressionTransform() {
    this->Outputs.AddDefaulted(1);
    this->TransformSourceType = TRANSFORMSOURCE_Tangent;
    this->TransformType = TRANSFORM_World;
}


