#include "AnimNode_AnimDynamics.h"

FAnimNode_AnimDynamics::FAnimNode_AnimDynamics() {
    this->SimulationSpace = AnimPhysSimSpaceType::Component;
    this->bChain = false;
    this->GravityScale = 0.00f;
    this->bLinearSpring = false;
    this->bAngularSpring = false;
    this->LinearSpringConstant = 0.00f;
    this->AngularSpringConstant = 0.00f;
    this->bEnableWind = false;
    this->bWindWasEnabled = false;
    this->WindScale = 0.00f;
    this->bOverrideLinearDamping = false;
    this->LinearDampingOverride = 0.00f;
    this->bOverrideAngularDamping = false;
    this->AngularDampingOverride = 0.00f;
    this->bOverrideAngularBias = false;
    this->AngularBiasOverride = 0.00f;
    this->bDoUpdate = false;
    this->bDoEval = false;
    this->NumSolverIterationsPreUpdate = 0;
    this->NumSolverIterationsPostUpdate = 0;
    this->bUsePlanarLimit = false;
    this->bUseSphericalLimits = false;
    this->CollisionType = AnimPhysCollisionType::CoM;
    this->SphereCollisionRadius = 0.00f;
}

