# Play N-Gage 2.0 on Symbian^3

Nokia N97's N-Gage 2.0 (version 1.30), adapted for hacked Symbian^3 and Belle
phones, with on-screen touch controls.

Tested on a **Nokia 808 Belle**: every game tested so far runs.

![In-game touch controls](docs/images/game-controls.png)

## Before you start

- Your phone must be hacked, with **Install Server** enabled and **Open4All** disabled.
- Games are not included. You need your own `.n-gage` game files.

## Install

1. Download **`ngage-installer-symbian3.sis`** from the
   [latest release](https://github.com/yeatse/ngage-installer-symbian3/releases/latest).
2. Copy it to your phone and install it. No signing is needed. When the phone
   asks where to install, any drive works, for example the memory card if the
   mass memory is unusable. A few settings files always go to phone memory (`C:`).
3. Copy your `.n-gage` game files to an `n-gage` folder at the top of any drive,
   for example **`E:\n-gage`** or **`F:\n-gage`** on the memory card.
4. Open **Games**, install a game there, then tap its cover to play. Games asks
   which drive to install each game to.

To upgrade, close Games, install the new package over the old one, then restart
the phone.

## Controls

- A five-way pad is on the left; R, A, B and L are on the right.
- The pad has eight directions: touch between two arrows for a diagonal, and slide
  your thumb to change direction without lifting it.
- The button at the upper left hides or shows the controls.
- The button at the lower left switches the right side between A/B and a number keypad
  with a C key, for games that use number keys or need text entered.
- The phone's volume keys change the game volume.
- On phones with a keyboard (E6, E7), keyboard keys can be mapped to game buttons;
  see [Keyboard mapping](#keyboard-mapping).

## Keyboard mapping

Without a mapping file, keyboard keys work as on the N97: the arrow keys are the
pad, Enter is A, Backspace is B, Q to P are 1 to 0, J is * and H is #.

To choose your own keys, create a text file named **`keymap.ini`** in
**`C:\Data\N-Gage`** (phone memory) with one `Key = Button` per line:

```ini
; ';' starts a comment
Q = A
A = B
W = 1
E = 2
R = 3
K = 7
Left = Right
```

- **Key** is a keyboard key: `A` to `Z`, `0` to `9`, `Space`, `Enter`, `Backspace`,
  `Up`, `Down`, `Left`, `Right`, `Comma`, `Period`, or a scan code such as `0x79`.
- **Button** is a game button: `Up`, `Down`, `Left`, `Right`, `OK`, `A`, `B`, `L`, `R`,
  `0` to `9`, `*`, `#`, or `C` (the keypad's clear key).
- Names are not case-sensitive. If a key is listed twice, the first line counts;
  lines that are not understood are ignored.
- A mapped key no longer reaches the game as itself; keys that are not listed keep
  working as before. The mapping applies only while a game is in the foreground.
- The file is read when a game starts: restart the game after changing it.

A USB connection shows only the mass memory and the memory card, not `C:`. Copy
the file to the phone, then move it to `C:\Data\N-Gage` with the phone's file
manager, or create it there with a text editor.
[`docs/keymap-example.ini`](docs/keymap-example.ini) is a complete example with
an E7 layout.

## More

- [How the adaptation works](docs/adaptation-summary.md)
- [Building from source](docs/build.md)
- [Validation and development history](validation/README.md)

Nokia software and game imagery belong to their respective owners and are not
covered by this project's MIT license.
