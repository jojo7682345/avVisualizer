#include "ecsQuerries.h"

bool32 isQuerrySelected(SelectionAccessCriteria criteria, ComponentMask mask){
    ComponentMask required = componentMaskOr(criteria.requiredRead, criteria.requiredWrite);
    ComponentMask present = componentMaskAnd(required, mask);
    if(!componentMaskEquals(present, required)){
        return false;
    }
    if(!componentMaskIsEmpty(componentMaskAnd(criteria.excluded, mask))){
        return false;
    }
    return true;
}

