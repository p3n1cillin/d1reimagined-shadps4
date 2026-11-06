# PC controls for Destiny (CUSA00219, build 56578)

Set `D1_PC_LAYOUT=1` to play with a keyboard and mouse. It turns on:

- **a keyboard layout after Destiny 2's PC defaults**, wherever D1 has the same action. The keys press
  the pad buttons the game already reads, so every action the controller has is on the keyboard, and
  the game's own rules for each button (tap or hold, the super's two-button chord) work as on the
  console;
- **raw mouse look** in gameplay;
- the **mouse buttons**, aware of menus;
- the **menu cursor** on the mouse;
- **Esc**, aware of menus;
- **a connected pad slot when the pad is unplugged**, so the keyboard carries on.

A controller keeps working beside the keyboard, with the console's own aim assist.

## The layout

| Action | Key | Pad button |
|---|---|---|
| Move | W A S D | left stick |
| Look | mouse | right stick |
| Jump | Space | cross |
| Sprint | Left Shift | L3 |
| Crouch | Left Ctrl | circle |
| Fire | left mouse button | R2 |
| Aim | right mouse button | L2 |
| Reload (tap) | R | square |
| Interact (hold) | E | square |
| Melee | C | R1 |
| Middle mouse button | middle mouse button | R3 |
| Grenade | Q | L1 |
| Super | F | L1 + R1 |
| Switch weapon | 1, 2, \` (grave), mouse wheel | triangle (a tap swaps primary and special) |
| Heavy weapon | hold 3 | hold triangle |
| Emotes | arrow keys | d-pad |
| Character screen | I, or Esc in play | Options |
| Ghost / Director | Tab, M | touchpad |
| Confirm in menus | Enter, left mouse button | cross |
| Back in menus | Esc, right mouse button | circle |
| Mouse 4 / Mouse 5 | reload or interact / crouch | square / circle |

Destiny 2 has actions that D1 does not. These are left out: class ability, air move, finisher and
text chat. D1 has no separate kinetic, energy and power slots. Its triangle swaps the primary and
special weapons on a tap and draws the heavy weapon on a hold, so 1 and 2 both swap.

## Remapping

With `D1_PC_LAYOUT=1`, the emulator reads `user/input_config/d1-pc.ini` instead of the game's own
input file. It writes that file from the built-in layout the first time it starts, so you can edit it
and press F8 in game to reload. Delete it to get the default back. The syntax is upstream shadPS4's
(`documents/` and the emulator's Help).

Leave the mouse buttons and Esc out of that file: the D1 mouse layer handles them, so that they
confirm and go back in menus and fire and aim in play. The emulator's own hotkeys are in
`user/input_config/global.ini` (F2-F12, Delete, keypad +/-), and the layout leaves them free. F4
removes the virtual user, which signs the player out, so don't bind it to a game action.

## Mouse look

The mouse movement is added to player 0's look rates at the game's own look site (0x91C7DC). Each
frame it is scaled by the frame's time, so a given hand movement turns the camera by the same angle
at 30, 60, 144 or 240 frames per second. It is read as raw input: SDL's relative mode, which uses
Windows raw input. That means no pointer acceleration and no cursor re-centring. Relative mode is
on while no menu cursor is up and the game window has focus. Menus get the normal cursor back.

| Variable | Effect |
|---|---|
| `D1_MOUSE_LOOK_SENS=x` | Look sensitivity (default `D1_MOUSE_SENS`, else 1). |
| `D1_MOUSE_ADS_SENS=x` | Multiplies it while aim (L2) is held (default 1). |
| `D1_MOUSE_INVERT_Y=1` | Inverts the mouse's pitch. The game's own invert setting acts on the stick only. |
| `D1_MOUSE_SENS=x` | The menu cursor's speed (default 1, the engine's own rate), and the look's when `D1_MOUSE_LOOK_SENS` is unset. |
| `D1_MOUSE_RAW=0` | The earlier look route, which re-centres the system cursor every frame. |

**Aim assist.** The mouse gets no reticle friction or slowdown, as on Destiny 2's PC mouse. The game's
console aim assist is part of its stick processing, which the mouse doesn't go through. Bullet
magnetism belongs to each weapon in the game's own data and is unchanged. For the console's full
aim assist, aim with a controller. To feed the mouse through the right stick instead, so the
console's assist and acceleration both apply, use upstream's `mouse_to_joystick` binding in
`d1-pc.ini` (F7 toggles it) and leave `D1_MOUSE_LOOK` off. It feels like a stick, not a mouse.

## Controllers

SDL3 maps standard controllers (DualShock 4, DualSense, Xbox and most others) to the PS4 pad, and
the layout's controller section binds them one to one. Plugging a controller in at any time gives
it player 1's slot. With `D1_PC_LAYOUT=1`, unplugging it leaves that slot connected and driven by the
keyboard, as at a start with no controller. `D1_PAD_KEEP_KEYBOARD=0` keeps upstream's disconnect,
which the game shows as a disconnected controller, as on the console. `D1_PAD_KEEP_KEYBOARD=1` turns
the keyboard fallback on without the layout.

## The engine's own keyboard layer

`D1_KBM=1` (with `D1_KBM_MAP`) is the separate route through the game's built-in developer
keyboard layer (`d1/README.md`). It has no sprint, grenade, melee or super. Don't combine it with
`D1_PC_LAYOUT`, or the letter keys act twice.
