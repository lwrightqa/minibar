# Tasks

## Active

- [ ] **Weather screen: put the 64 px icons into the Low Glare Pixel mock** - wind icon still generating
- [ ] **Weather screen: review the XP-style icon set for Bold Signal** - eight icons generating (seed 403 sun picked)
- [ ] **Weather data layer** - `weather_proto.c` (URL builder, parser, WMO code to icon), host tests
  - header is written: `firmware/components/weather/include/weather_proto.h`
- [ ] **Weather on the device** - 15-minute task, city setup on the Remote, swipe screen, °C/°F setting
- [ ] **Low Glare Pixel theme in firmware** - edge bar, larger Handjet sizes, pixel layouts, Display menu, toast, Tap sound
- [ ] **Flash and check Bold Signal** - after the palette swap; backups of nvs and nvs_sec are in the scratchpad
- [ ] **Open pull requests** for the feature branches (todo, theme setting, palette, font, weather mock)
- [ ] **Todo editor on the Remote** - short-name format hint and the 18-character limit

## Waiting On

- [ ] **PR #3 merge** - test fixes for the Remote page test, since 2026-10-09

## Someday

- [ ] **Calendar host test** - `merge_trims_to_keep_over_ones_first` fails on `main`
- [ ] **Jira progress line chart** - show a line chart next to the Jira tickets with the ups and downs of progress
- [ ] **Cold icon** - no cold-weather icon yet
- [ ] **Encrypt nvs_sec** - needs an eFuse key (see ARCHITECTURE.md "Secrets")
- [ ] **Delete old branches** - `low-glare-pixel-theme`, `jira-secrets-and-remote-copy`, `backup/wip-before-split` once merged

## Done

- [x] ~~Jira token and email moved to nvs_sec, goal values fixed (PR #2)~~ (2026-10-09)
- [x] ~~Split the work into feature branches and pushed them~~ (2026-10-10)
