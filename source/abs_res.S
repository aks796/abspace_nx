/* abs_res.S -- files built into the program: the controller script that
 * runs inside the game's Lua (abs_lua.c), the one that adds Orbital
 * Escapade's worlds (abs_orbital.lua), and the hand cursor's pictures
 * (abs_cursor.c). Paths are relative to the project root, where make runs.
 * MIT. */
	.section .rodata.abs_res, "a"
	.balign 4
	.global abs_ctl_lua
abs_ctl_lua:
	.incbin "source/abs_ctl.lua"
abs_ctl_lua_end:
	.byte 0
	.balign 4
	.global abs_ctl_lua_size
abs_ctl_lua_size:
	.word abs_ctl_lua_end - abs_ctl_lua

	.balign 4
	.global abs_orbital_lua
abs_orbital_lua:
	.incbin "source/abs_orbital.lua"
abs_orbital_lua_end:
	.byte 0
	.balign 4
	.global abs_orbital_lua_size
abs_orbital_lua_size:
	.word abs_orbital_lua_end - abs_orbital_lua

	.macro abs_file name, path
	.balign 4
	.global \name
\name:
	.incbin "\path"
\name\()_end:
	.balign 4
	.global \name\()_size
\name\()_size:
	.word \name\()_end - \name
	.endm

	abs_file abs_hand_point_png, source/cursor/hand_point.png
	abs_file abs_hand_click_png, source/cursor/hand_click.png
	abs_file abs_hand_hover_png, source/cursor/hand_hover.png
	abs_file abs_hand_grab_png, source/cursor/hand_grab.png
