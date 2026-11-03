# Jester Jack audio

All files came from the team’s “JESTER JACK” folder. Filenames use ASCII; the game looks for these exact paths (`src/audio/Mixer.cpp`).

| File | Original source name | When it plays |
| --- | --- | --- |
| `sfx/card_deal.mp3` | Dealer's distribution of cards | For each card dealt to the player and dealer |
| `sfx/card_roll.mp3` | The player's card roll | When the player hits or chooses a card (HIT), and when the dealer draws |
| `sfx/chips_start.mp3` | giving chips to player and bot | At the start of a new run |
| `sfx/chips_bet.mp3` | the bet of the chips | When a hand is dealt or a relic is purchased |
| `sfx/chips_win.mp3` | winning chips | When the player receives coins; when the dealer takes a lost hand's bet; when a relic is sold |
| `sfx/tada_minor.mp3` | tada minor sound | When a hand is won |
| `sfx/tada_major.mp3` | tada major sound | When a hand that completes the round target is won |
| `sfx/you_lose.mp3` | you lose sound | At the end of a run |
| `ui/ui_open.mp3` | UI Open | When an interface button is pressed |
| `ui/ui_close.mp3` | UI Close | Escape, BACK, DONE, GOT IT, MENU |
| `ui/ui_selection.mp3` | UI Selection | When the cursor enters a button, menu only (not during a run) |
| `ambient/car_pass.mp3` | car driving by (normalized by +6 dB with a limiter) | As the car drives along the main-menu street; the recording is tied to the car's position |
| `ui/ui_error.mp3` | UI Error | An invalid command or unavailable purchase |
| `music/main_theme.mp3` | JESTER JACK - Main theme | Loops in the menu |
| `music/soundtrack_1.mp3` ... `soundtrack_5.mp3` | JJ - soundtrack 1 ... 5 | At the table, in shuffled looping order |

`music/soundtrack_N.sync` contains the beat grid and volume curves for the stage ensemble. Generate it with `python scripts/analyze_soundtrack.py assets/audio/music/soundtrack_*.mp3` (requires ffmpeg, numpy, and scipy). For a new soundtrack without a `.sync` file, the musicians play at the default tempo.

To add more soundtracks, add `soundtrack_N.mp3` to `music/`; they are detected automatically.

Recording authorship and licenses are not specified. The project owner should add them here (see `THIRD_PARTY_NOTICES.md`).
