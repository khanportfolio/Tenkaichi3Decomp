# Tenkaichi Budokai Online: credits for the pictures

The pictures of the Tenkaichi Budokai Online screens (this folder; `tbo/ui/` in a release).

- **The announcer** (`hosts_*.png`): by **MarshallPunk**.
- **The Dragon Radar** (`radar.png`): fan art by **chestergreen**. Changed for the port: every second frame of the
  animation, scaled and laid out as one sheet of frames.
- **The buttons** (`btn_*.png`): by **pluantoine**, under the
  [Creative Commons Attribution 4.0 International license](https://creativecommons.org/licenses/by/4.0/) (CC BY 4.0).
  Changed for the port: cut out of the sheet, their background removed and scaled; the left and right directions
  combined into one picture (`btn_leftright.png`).

- **The Mod Manager** (`mm_*.png`): contributed with the Mod Manager (src/menu/port_modmgr.c); their sizes are
  `include/menu/mm_sizes.h`, written by `python3 port/tools/mm_sizes.py`.

A picture replaced by one of another size: run `python3 port/tools/tbo_sizes.py` (the screens lay the pictures out
by their sizes, `include/menu/tbo_sizes.h`).
