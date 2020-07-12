#include "normalisation.h"

OBS_DECLARE_MODULE()

bool obs_module_load() {
    obs_register_source(&normalisation_source);
    return true;
}
