#include "KinematicAnalysisUiLogic.hpp"

bool rws::ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked)
{
    return !checkboxAvailable || checkboxChecked;
}
