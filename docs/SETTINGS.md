# Settings

The settings page in the game (**F2**, or **Mod Settings** on the Remastered page) changes everything below. It's all
stored in `mods/remastered-mod.ini` in the game folder, which the game makes at its first start from
`remastered-mod.default.ini`; updates add new settings to it and keep yours. You can also edit it with any text
editor: comments go on their own lines, starting with `;`, and every setting has one in the file.

A mod can be switched off in `[mods]`: `name=0` (e.g. `mutators=0`), or on the settings page's **Mods** tab.

Keys are Windows key codes (112 = F1 ... 123 = F12, 32 = Space, 65-90 = A-Z; 0 = no key); the **Keys** tab is easier.

```ini
[core]
enabled=1          ; 0 = Insaniquarium - Remastered Mod off (same as holding Shift at start, but permanent)

; keys (easier: the Keys tab): Windows key codes, 112 = F1 ... 123 = F12, 32 = Space, 65-90 = A-Z; 0 = no key
[settings]
open_key=113       ; F2

[display]
window=native      ; a large window; normal = the game's 640x480 window; borderless = fills the screen
scale=fit          ; integer = only whole multiples (2x, 3x...): sharpest pixels, wider black bars

[autosave]
minutes=2          ; 0 = only when the game pauses
label=1            ; 0 = no "Autosaved" note

[continues]
enabled=1
price=500          ; first continue; each one after costs this much more

[accessibility]
hungry=0           ; 1 = a red "!" over hungry fish
coins=0            ; 1 = each coin's value under it

[mutators]
hungry=0           ; 1 = on (from the next level). Also: double, glass, nopets, pacifist, heavycoins, tinywallet,
richstart=0        ; hard, golden, combos, events, overeat, decay, gadgets (see the comments in the file)
gadgets_key=120    ; F9: shows or hides the gadgets (or the Other Gadgets button, bottom right of the tank)

[achievements]
banners=1          ; 0 = no banner when one unlocks
open_key=115       ; F4

[hovercoins]
enabled=1          ; 0 = click coins as usual

[coop]
open_key=118       ; F7 on the main menu
chat_key=13        ; Enter in a co-op game or lobby: a chat line
port=27615         ; the TCP port the host listens on (open it in your router to host over the internet)
name=              ; your name in co-op (empty = your profile's name)
avatar=1           ; your avatar in co-op: 0 none, 1 guppy, 2-31 the pets and special fish (or the Avatar button)
server_host=       ; online co-op server; empty = the mod's public server (or set it in the co-op screen's Server tab)
server_port=443
server_tls=1       ; 1 = https (internet), 0 = plain http (a server on your own network, usually port 7350)
key=               ; that server's key (not a password: every player of the server has it)

[fps]
key=114            ; F3
shown=0            ; 1 = on from the start

[screenshot]
key=123            ; F12

[screensaver]
key=122            ; F11 on the main menu (0 = no key)
quit=0             ; 1 = close the game while the screensaver shows (under Steam that ends the screensaver too)

[timecontrol]
pause=116          ; F5
slow=117           ; F6
fast=119           ; F8
menu=32            ; Space: the game's own pause dialog
```
