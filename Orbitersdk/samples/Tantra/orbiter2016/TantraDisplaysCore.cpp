// TantraDisplays - the energy core for the plant screen: the core's snapshot (core/TantraCore, Tantra::CoreState()) handed to
// TantraPlantScreen as it is - the nodes' states and reasons, the ВЭУ, the field store, the feed, the trigger, the windings, the
// cryo, the reaction mass, the pumps, the jacket, the cup, the stern, the radiators. The screen reads only these (and the plant's
// own output); it computes no reading of its own. Called at the end of FillPlantView.
#include "TantraDisplays.h"
#include "Tantra.h"

void TantraDisplays::FillCoreView(tantra::plantscreen::View& v) const {
    v.core = t_->CoreState();
}
