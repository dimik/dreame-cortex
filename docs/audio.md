# Audio playback (speaker / voice / TTS)

How to play arbitrary audio — beeps, voice, TTS — on the robot's built-in speaker, reusing Dreame's
own audio stack so it never fights AVA for the ALSA codec.

## The Dreame audio architecture (reverse-engineered)

```
mda_cli "single,<path-no-ext>,<vol>"  →  mediad (pid, TCP 127.0.0.1:10100)  →  /ava/script/mediad_script.sh
                                                                                  ├─ amixer -D hw:audiocodec 'LINEOUT volume'  (MR813)
                                                                                  └─ ogg123 <path>.ogg  →  ALSA codec  →  speaker
```

- **`mediad`** (started `mediad -c /ava/script/mediad_script.sh`, monitored by `mediad_monitor.sh`)
  is the media daemon. It **listens on TCP `127.0.0.1:10100`** and owns the speaker.
- **`mda_cli "<message>"`** just sends a string to `:10100`. Messages AVA uses:
  - `play,<path>` — play
  - `single,<path>,<vol>` — play one file at a volume, restoring volume after
  - `ret` — (return/restore)
  - The `<path>` is given **WITHOUT extension** — `mediad_script.sh` appends `.ogg`.
- **`mediad_script.sh`** kills any running `ogg123` first (so audio is **serialized** — your sound and
  AVA's own prompts queue through one daemon, never overlapping on the codec), sets volume via
  `amixer -D hw:audiocodec` (max 31 on this MR813 board), then `ogg123 <path>.ogg`.

**Why go through mediad, not `aplay`/`ogg123` directly:** `aplay` opens ALSA `hw:0,0` directly and can
collide with AVA's `ogg123` (device busy / cut-off). mediad is the one owner — no contention, plus
free volume handling. (The repo's old `aplay`-based `audio_server.py` is superseded by this.)

**Wire protocol** (confirmed): connect to `127.0.0.1:10100`, send the message string — no terminator
needed. The chroot can reach it (shares the host network), so no `mda_cli` binary is required there.

## Play something — quickest path

```sh
# any OGG in /tmp:
oggenc -Q input.wav -o /tmp/hello.ogg          # WAV -> OGG (oggenc is on the robot)
mda_cli "single,/tmp/hello,70"                 # plays /tmp/hello.ogg @ vol 70  (NOTE: no .ogg)
```
Generate a tone from nothing (ffmpeg is in the chroot at `/opt/ffmpeg`):
```sh
chroot /data/chroot /opt/ffmpeg -f lavfi -i "sine=frequency=660:duration=1.5" -ar 16000 /tmp/t.wav
oggenc -Q /tmp/t.wav -o /tmp/t.ogg && mda_cli "single,/tmp/t,70"
```

## ROS integration — `audio_bridge.py` (text→speech + files)

`scripts/robot/audio_bridge.py` (chroot ROS, started by `_root_postboot.sh`) subscribes
**`/robot/speak`** (`std_msgs/String`); the message is one of:
- **text** → spoken via on-robot TTS (espeak-ng) — `"robot, say X"` in one line
- a readable **`.ogg` path** → played as-is
- **`"stop"`** → `killall ogg123`

```sh
ros2 topic pub --once /robot/speak std_msgs/msg/String "{data: 'Docking complete'}"     # speaks it
ros2 topic pub --once /robot/speak std_msgs/msg/String "{data: /tmp/hello.ogg}"          # plays file
```
Params: `volume` (90), `voice` (`en-us+f3`), `speed` (155 wpm), `amplitude` (0–200).

## On-robot TTS — espeak-ng

Installed in the chroot (reproducible):
```sh
chroot /data/chroot apt-get -o APT::Sandbox::User=root install -y --no-install-recommends espeak-ng
```
The bridge runs `espeak-ng -v <voice> -w x.wav <text>` → `/opt/ffmpeg -c:a libvorbis x.ogg` → mediad
(both self-contained in the chroot; the chroot ffmpeg has libvorbis). espeak-ng is **robotic formant
synthesis** but instant and tiny.

**Choose a voice** — set the `voice` param to any espeak-ng variant (sampler heard: `en-us`,
`en-us+f3`, `en-us+f5`, `en-us+m3`, `en-us+m7`, `en-gb`, `en-us+whisper`; full list:
`espeak-ng --voices`):
```sh
ros2 param set /audio_bridge voice en-gb        # or bake the default into audio_bridge.py
```

## Natural voices (optional upgrade) — do TTS on the companion

For natural (neural) speech, synthesize on the Q6A (the A7 is too weak for Piper-quality at speed)
and ship the OGG over. `companion/tts_speak.sh`:
```sh
./tts_speak.sh "Docking started" [robot-host]
# Piper (preferred) or espeak-ng -> WAV -> oggenc -> scp robot:/tmp -> trigger /robot/speak (or mda_cli)
```
Pipeline: `LLM/text → TTS (Piper/Kokoro/Coqui/OpenAI) → WAV → oggenc → robot:/tmp → mediad`.

## Caveats

- **OGG only** through mediad (it appends `.ogg` + runs `ogg123`). Encode WAV→OGG with `oggenc`.
- **Serialized**: a new play kills the previous `ogg123` — no overlap/mixing (matches AVA's behavior).
- Volume is 0–31 on MR813 but `mda_cli` takes the AVA-scale value; the bridge passes it through.
