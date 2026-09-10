#ifndef GM_CAT_ANIM_PARAMS_H
#define GM_CAT_ANIM_PARAMS_H

// The animation Parameters page (gm-3vj.2), pushed from the Animation
// category's second row. This header is the whole contract between the two
// files: CatAnimation.cpp owns the animation roster (and, on the simulator,
// the mirror of it) and exports the three accessors below; CatAnimParams.cpp
// owns the page and exports the one call that pushes it. The page's own
// SettingsCategoryDef stays in that file's anonymous namespace, the way
// CatSchedules.cpp's two pushed pages do: it is never in SettingsUI.h's tile
// registry and is reached only by pointer from the row that pushes it.
#include "SettingsUI.h"

#include <display/ui/default/bganim/BgAnim.h>

// The animation roster, as the settings screens see it. On the device these
// are bg_animation_count()/bg_animation(id).name/.params; on the simulator
// the registry does not compile (it carries every render kernel), so they
// come from CatAnimation.cpp's generated mirror instead. Defined in
// CatAnimation.cpp; every id is clamped into range, so no caller has to.
int settingsAnimCount();
const char *settingsAnimName(int animId);
// BG_ANIM_PARAMS entries, in slot order. A slot whose `key` is nullptr is
// one this animation does not define; the Parameters page shows the rest.
const BgAnimParamDef *settingsAnimParams(int animId);

// Pushes the Parameters page for `animId`. Called from the Animation
// category's row handler, which is on the UI task with the page stack
// settled, so this may push straight away.
void settingsAnimParamsPush(SettingsUI &ui, int animId);

#endif // GM_CAT_ANIM_PARAMS_H
