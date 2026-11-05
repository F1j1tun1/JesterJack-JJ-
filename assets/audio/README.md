# Jester Jack audio

The project owner supplied 20 MP3 recordings in the folder named `Аудио`. The game uses the 19 files mapped below: 18 are byte-for-byte copies of their supplied originals, while the car recording has the documented gain-only export. Runtime paths use ASCII and are defined in `src/audio/Mixer.cpp`.

| Runtime file | Exact supplied filename | Event |
| --- | --- | --- |
| `sfx/card_deal.mp3` | `Giving cards to the player and the bot.mp3` | Each opening card dealt to either participant, including natural-blackjack hands. |
| `sfx/card_roll.mp3` | `The player's card roll.mp3` | A player HIT or an extra card drawn by the dealer; the opening deal uses the separate file above. |
| `sfx/chips_start.mp3` | `giving chips to player and bot.mp3` | Start of a new run. |
| `sfx/chips_bet.mp3` | `the bet of the chips.mp3` | Beginning a hand or paying for a relic. |
| `sfx/chips_win.mp3` | `winning chips.mp3` | Winning coins, selling a relic, or the dealer collecting chips after a lost hand. |
| `sfx/tada_minor.mp3` | `tada minor sound.mp3` | A won hand that does not complete the round goal. |
| `sfx/tada_major.mp3` | `tada major sound.mp3` | A won hand that completes the round goal. |
| `sfx/you_lose.mp3` | `you lose sound.mp3` | A lost hand; the same loss must not trigger a second copy when GAME OVER appears. |
| `ui/ui_open.mp3` | `UI Open.mp3` | Normal button activation/opening an interface panel. |
| `ui/ui_close.mp3` | `UI Close.mp3` | Closing/back actions, including Escape, BACK, DONE, GOT IT and MENU. |
| `ui/ui_selection.mp3` | `UI Selection.mp3` | Entering a button with the pointer, across interface screens. |
| `ui/ui_error.mp3` | `UI Error.mp3` | A rejected command or unavailable purchase. |
| `ambient/car_pass.mp3` | `car driving by.mp3` | A menu sedan pass, aligned to the same scene clock as its movement. |
| `music/main_theme.mp3` | `JESTER JACK - Main theme.mp3` | Menu theme, repeated until gameplay starts. |
| `music/soundtrack_1.mp3` | `JJ - soundtrack 1.mp3` | Gameplay shuffle playlist. |
| `music/soundtrack_2.mp3` | `JJ - soundtrack 2.mp3` | Gameplay shuffle playlist. |
| `music/soundtrack_3.mp3` | `JJ - soundtrack 3.mp3` | Gameplay shuffle playlist. |
| `music/soundtrack_4.mp3` | `JJ - soundtrack 4.mp3` | Gameplay shuffle playlist. |
| `music/soundtrack_5.mp3` | `JJ - soundtrack 5.mp3` | Gameplay shuffle playlist. |

`Dealer's distribution of cards.mp3` is the unused twentieth supplied file. It was the former `card_deal.mp3`; it has been replaced by the specifically requested `Giving cards to the player and the bot.mp3` (20,736 bytes, decoded duration 0.648 s). Do not substitute the former dealer recording again. The deal recording is kept unchanged; its playback gain is 0.9 to leave room for a small MP3 decoder overshoot above 0 dBFS.

Sound names involving bets describe the recordings; they do not add betting mechanics or change the game economy. Initial-deal cues map to `Sfx::CardDeal`; player/dealer extra-card cues both map to `Sfx::CardRoll`. UI requests made in one frame are resolved by the interface with priority Error > Close > Open > Selection, so a hover does not cover a click/error sound.

## Loops and playback

There are six supplied music recordings: one menu theme and five gameplay soundtracks. Every gameplay shuffle cycle plays each discovered `music/soundtrack_*.mp3` once; new cycles continue indefinitely without immediately repeating the last track when multiple tracks are available. The menu theme loops separately. A missing or unreadable track is skipped, even across a shuffle-cycle boundary; if all tracks fail, retries stop until the music mode changes.

Two voices per one-shot allow quick card/UI sounds to overlap without opening a new decoder every time. Default gains: sound effects 1.0, UI 0.8, music 0.5, car 0.9; the deal-only trim multiplies its effect gain by 0.9. Music ducking multiplies music by 0.35. Hidden runs are silent. Windows uses its built-in MCI/winmm decoder on a playback thread; other platforms currently keep the same API but produce no sound.

OPTIONS exposes Music and Effects master volume, from 0% (mute) to 100% (the mix above). Effects includes cards, chips, fanfares, UI and the menu car. Changing a slider also changes sounds already playing, preserving music position, ducking and car fades. Windows stores these two preferences in `%LOCALAPPDATA%/JesterJack/audio.ini`; they load before playback starts. Hidden diagnostics use defaults unless given an explicit `--audio-settings FILE`.

## Car recording and timing

The original `car driving by.mp3` is 346,080 bytes. It decodes to 17.304 s, with whole-file RMS −33.306 dBFS and sample peak −7.319 dBFS near 4.940 s. Its strongest 100 ms energy window is 4.9–5.0 s. The exported runtime file raises the recording by 5 dB, retaining its speed, order, leading sound and quiet receding tail. No trimming, time stretching or limiter is used:

```text
ffmpeg -i "car driving by.mp3" -map_metadata -1 -af "volume=5dB" -codec:a libmp3lame -b:a 192k -ar 48000 -write_xing 1 car_pass.mp3
```

The export is 416,492 bytes: decoded duration 17.304 s, RMS −28.566 dBFS, sample peak −2.557 dBFS at 4.940021 s, and no decoded samples at or beyond full scale. With the runtime car gain of 0.9 it retains additional headroom. This replaces the older undocumented limiter export, whose measured RMS was about 6.75 dB louder than the original.

The sedan repeats every 32 s and crosses the street over the first 9.4 s; its visual midpoint is phase 4.7 s. Starting the recording at `scenePhase + 0.24` aligns its 4.94 s peak with that midpoint, including when the menu is entered partway through a pass. After the sedan leaves, the remaining recording fades over 1.2 s; leaving the menu, starting gameplay or disabling Ambient motion also fades it. These offsets come from the recording, not from gameplay randomness. Natural playback completion clears the mixer's car-playing state; a short decoder startup grace prevents false completion.

SHA-256 provenance:

| File | SHA-256 |
| --- | --- |
| Supplied `car driving by.mp3` | `A66FBC7AEC32C3767C7A57C7F3985CC8C94AA936CA215FBB8D36DE8C0E3B52EE` |
| Exported `ambient/car_pass.mp3` | `0D4BB023EBB937C210DA1A49BCFCBA668B978F9EFEE03F2C80D44274C5D222E0` |
| Supplied/copy `sfx/card_deal.mp3` | `48320FB89F9D5BD21BBC02E848F1E8AF9FC8733C0EAF58ECD9D5B99511FE5032` |

## Soundtrack sync data

`music/soundtrack_N.sync` stores a beat grid and four 4 Hz energy curves (low, mid, high and percussive attack). Generate them from the matching MP3 files with:

```text
python scripts/analyze_soundtrack.py assets/audio/music/soundtrack_1.mp3 assets/audio/music/soundtrack_2.mp3 assets/audio/music/soundtrack_3.mp3 assets/audio/music/soundtrack_4.mp3 assets/audio/music/soundtrack_5.mp3
```

Offline analysis requires ffmpeg, numpy and scipy; they are not runtime dependencies. The menu theme has no stage-ensemble sync file. Additional `soundtrack_N.mp3` files are discovered automatically; without a companion `.sync`, the ensemble uses its fallback tempo.

| Soundtrack | Duration in `.sync` | Median tempo | Beat-grid downbeat offset |
| --- | ---: | ---: | ---: |
| 1 | 153.62 s | 103.45 BPM | 0 |
| 2 | 123.04 s | 157.89 BPM | 3 |
| 3 | 180.31 s | 130.43 BPM | 3 |
| 4 | 96.82 s | 125.00 BPM | 1 |
| 5 | 97.91 s | 78.95 BPM | 3 |

The energy windows use exact 0.25 s boundaries on the 50 Hz analysis grid, alternating 12 and 13 frames. A fixed rounded 12-frame stride would incorrectly stretch energy motion by about 4.17% over a song. The five curves were regenerated after correcting this drift; their durations, tempos, downbeat offsets and beat timestamps are unchanged.

Recording authorship and licenses were not specified with the supplied files. The project owner should supply that attribution here and in `THIRD_PARTY_NOTICES.md`; no authorship is inferred from the filenames.
