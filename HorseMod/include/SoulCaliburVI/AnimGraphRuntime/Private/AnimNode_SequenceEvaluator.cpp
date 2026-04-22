#include "AnimNode_SequenceEvaluator.h"

FAnimNode_SequenceEvaluator::FAnimNode_SequenceEvaluator() {
    this->Sequence = NULL;
    this->ExplicitTime = 0.00f;
    this->bShouldLoop = false;
    this->bTeleportToExplicitTime = false;
    this->StartPosition = 0.00f;
    this->ReinitializationBehavior = ESequenceEvalReinit::NoReset;
    this->bReinitialized = false;
}

