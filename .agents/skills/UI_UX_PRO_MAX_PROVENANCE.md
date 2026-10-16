# UI UX Pro Max development skills

Installed project-locally from the official `ui-ux-pro-max-cli@2.15.0` npm package with `uipro init --ai codex --offline`. The installer supplied `ui-ux-pro-max` and the `banner-design`, `brand`, `design`, `design-system`, `slides`, and `ui-styling` companions. These development tools are not included in the portable application runtime.

- Upstream: https://github.com/nextlevelbuilder/ui-ux-pro-max-skill/tree/v2.15.0
- Package: https://registry.npmjs.org/ui-ux-pro-max-cli/-/ui-ux-pro-max-cli-2.15.0.tgz
- npm integrity: `sha512-D0J/C40xrzzi5si6ZLtRGbEE5v3QjL7d4wJNnasmP3yfDSrGiuqVCdwQiqCNnIkbqOuVoA/uonR2o1WKXh3urw==`
- Original upstream license: `UI_UX_PRO_MAX_LICENSE.txt` (MIT, Next Level Builder). The `ui-styling` companion also carries its own original `LICENSE.txt` (Apache-2.0), which remains in place.
- 171 bundled data/script/companion files were compared byte-for-byte with the npm package during installation. The pre-existing `imagidock-assets` skill was preserved.

The application stays C++20/Qt Widgets. Existing palette tokens are recorded in `docs/ui-tokens.baseline.json`; generated palettes and web/mobile-specific implementation instructions do not override the user-approved design.
