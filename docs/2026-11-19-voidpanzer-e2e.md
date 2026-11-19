# VoidPanzer v001 hardware check (2026-11-19)

## Setup

- FreeWili 2 (`FREE-WILi2 FX0036`) on the on-board CMSIS-DAP probe, Windows.
- wilibsp pinned at `0a89c13`. `tools\dev run`, then `tools\dev` press / hold /
  release / screenshot, log from `tools\dev rtt`.

## Observed results

- Boot log: `voidpanzer: v001 up, imu=1 agentio=1`; IMU and audio up.
- RAM: 52.9 KB text + 333.5 KB bss (284 KB framebuffer) of 512 KB SRAM.
- Setup screens render (title, prompts, live accelerometer, START over GREEN);
  two GREEN presses start a game (no gesture -> tilt fallback, logged).
- 3D view renders: ground grid, horizon mountains that turn with the heading,
  cube / pyramid / monolith obstacles, the enemy tank model, HUD (score, tank
  icons, missile rack, radar with enemy dot and sweep, reticle), button bar
  MISSILE / REV / DRIVE / FIRE over GRAY / YELLOW / GREEN / RED.
- D-pad LEFT/RIGHT turns the tank (bearing to the enemy changed as expected
  in the log); the reticle showed `LOCK` with the enemy at -3 deg, 65 m.
- GRAY launched a missile (`MISSILE left=3`).
- Enemies fire and hit (`HIT lives=2/1/0`), the game reaches `GAME OVER` and
  GREEN starts a new game.
- First tuning pass on hardware: enemies hit a stationary tank about every
  4 s, too lethal. Added aim error (+/-0.10 rad), slower fire (5-8 s), an
  80 m firing range and a 3 s withdrawal after the player is hit.
- `tools\dev test`: all game-rule tests pass (world generation, driving and
  obstacle stops, one shell per press with reload, shells blocked by
  obstacles, proximity near-miss kill, wide miss survives, one blast kills
  two, four missiles then EMPTY, hit -> life lost + full rack + enemies
  withdraw, game over, spawning, enemy turning, lock).
- Installed to `/apps/voidpanzer.uf2`.

## Not verified

- **A kill on hardware.** Remote aiming through the debug probe has about a
  second of latency per nudge; the enemy won each exchange. Kills by cannon
  and by missile are covered only by the host tests.
- Steering with a real turn or tilt, D-pad UP/DOWN and GREEN/YELLOW driving
  by a person, sound (not heard), LED colours, the eased enemy settings in
  play, frame rate under load, and the `RELEASE RED` warning.

## v002 (same day, after first play-test)

- Player asked for fewer obstacles (14 -> 7), the radar in the upper right,
  and the missile rack under the score and tank counter. Checked on the board
  by screenshot (battlefield, lock with `LOCK` at 0 deg / 71 m, game over).
- Host test for being hit now lets the enemy fire until a shot lands, since
  its aim error means any single shot may miss.

## v003 (2026-11-19)

Player report: the tank stayed in a hard right turn without the board being
rotated; under 15% of the steering range should do nothing.

- Cause: when the setup gesture was too small, the fallback steering axis was
  "the board axis most perpendicular to gravity", which on this board is the
  top-to-bottom (Y) axis, so tipping the board toward or away from the player
  read as a steady turn. The fallback is now the board's left-right (X) axis.
- Gyro (turn) mode now keeps re-learning the gyro's resting offset whenever
  the board is still, so a small offset cannot integrate into a turn.
- Steering is full at 40 deg with a 15% deadzone (6 deg), then linear.
- A steering gauge above the buttons shows the deadzone and the current
  steering (white = inside the deadzone, green = turning).
- Checked on the board: no-gesture setup logged "steering by board X axis";
  steer stayed 0 and heading stayed 0 deg for 8 s; gauge renders centred.
  Not checked: steering with a real tilt or turn.

## v004 (2026-11-19)

Player report: you can't tell when you are hit; is any sound supported?

- Sound bug: after any quiet spell the game powered the speaker path down with
  codec_nau88c10_speaker_low_power(), which also soft-mutes the DAC, and the
  restart only called codec_nau88c10_set_output(). The DAC stayed muted, so
  after the first ~0.4 s of silence (that is, from the start) nothing played.
  The restart now calls codec_nau88c10_dac_mute(false) first, as
  wilibsp/docs/drivers/audio.md says to. Same bug and fix in moonhop.
- Hit: jagged red cracks from a random impact point shoot across the screen and
  flash twice (on 0-0.22 s, off, on 0.36-0.62 s) with the red background.
  Hit sound is now its own: a crunch plus a 125 Hz square-wave grind, no longer
  the same boom as a kill.
- Cannon gained a low thump, the missile a swelling whoosh, the kill a sharp
  crack over the boom.
- Checked on the board: idle until an enemy hit me, the log shows HIT and the
  cracks render (screenshot docs/images/hit.png, taken with a test build that
  held the cracks for 5 s, because a screenshot takes 2-4 s). Not checked: I
  can't hear the board, so the sounds are untested by ear.
