# Third-party work

This repository contains none of the code below except where stated. Each is used under its own licence.

| Work | Authors | Licence | How it is used |
|---|---|---|---|
| ClownDR2Fix | Copyright (c) 2025 Tervel1337; fork by Clownacy | MIT | Loaded beside this plugin. `third_party/ClownDR2Fix-review-fixes.patch` is a patch against its source; the patch contains lines of its code and stays under its MIT licence. |
| safetyhook | cursey | Boost Software License 1.0 | Compiled into `DR2AP_core.dll` from the copy in the ClownDR2Fix source tree. |
| Zydis | Florian Bernd, Joel Höner | MIT | Compiled into `DR2AP_core.dll`, same source. |
| Ultimate ASI Loader | ThirteenAG | MIT | `dinput8.dll`, which loads `.asi` plugins. Not included. |
| Archipelago | Archipelago contributors | MIT | The world and client in `apworld/` run inside it. |

Dead Rising 2 is the property of Capcom. No game code, data, text, art or audio is included here; the tools in
`tools/` work on files from your own copy of the game.

If you redistribute a build, include the licence texts of the works compiled into it.
