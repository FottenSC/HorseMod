#pragma once
#include <cstdint>
namespace Horse::Deterministic {
// Only the body-state prerequisite. The enclosing owner separately validates
// scene phase, callbacks, constraints, shape/query consumers and native removal.
struct ReplayGroundBodyAdmission {
    template<class Read>
    static bool Flags(Read read,std::uintptr_t actor,unsigned body_flags,unsigned actor_flags) noexcept {
        if(actor_flags&4)return false;
        if(body_flags==2)return true;
        if(body_flags!=3)return false;
        // Shipped NpRigidDynamic::getKinematicTarget (39ED0) first consumes
        // buffered flags+17C. BodyCore E0180 then reads actor+130, tag+1F
        // and target-valid+1C. Public metadata7810 does not retain this target.
        // This initial-state path therefore admits only its absence; no bytes
        // are cleared to manufacture admission or restore a discarded target.
        unsigned buffered{};std::uintptr_t state{};unsigned char tag{},target{};
        return read(actor+0x17c,buffered) && !buffered
            && read(actor+0x130,state) && state
            && read(state+0x1f,tag) && tag==1
            && read(state+0x1c,target) && !target;
    }
};
}
