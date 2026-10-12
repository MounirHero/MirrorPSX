# Fonts, icon glyphs and attribution

MirrorPSX's native Vulkan/ImGui interface renders actual font glyphs through
Dear ImGui's atlas. It does not download icons, render a browser, or rely on
installed desktop fonts at runtime.

* **Figtree** variable font and two desktop instances (weight 400 and 650):
  [Figtree project](https://github.com/erikdkennedy/figtree), licensed under
  the **SIL Open Font License 1.1**, copyright The Figtree Project Authors.
  See `assets/licenses/figtree-OFL.txt`. The 400/650 files were instantiated
  from the corresponding OFL variable font for predictable desktop rendering.
* **Font Awesome Free 6.x, Solid**:
  [Font Awesome](https://github.com/FortAwesome/Font-Awesome), copyright
  Fonticons, Inc. The included desktop icon font is covered by **SIL OFL 1.1**
  (different from the CC BY 4.0 license for its source SVG files). See
  `assets/licenses/fontawesome-license.txt` for the original notice and terms.
* **DualSense action glyphs** (Cross, Circle, Triangle, Square, D-pad and R1)
  are simple native vector geometry implemented in
  `src/ui/screen.cpp`, not redistributed Sony font/brand assets.

The complete original licenses are shipped alongside the assets. Changing or
subsetting these fonts may create a derivative subject to the applicable
license terms and any reserved-font-name restrictions.

Windows CMake builds copy `assets/` alongside the application executable.
The host loads fonts from the working directory or the executable's asset
folder. Console packaging must bundle these same assets and supply the font
handles to `apply_screen_style` through the native PS5 host.
