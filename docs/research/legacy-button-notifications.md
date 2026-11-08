# Legacy button status notifications

GLS3 scripts consume `WndSpriteCmd.nNotification` using the seven-value
`EButtonSprite::ButtonStatus` enum in `EntisGLS/EntisGLS3/GLS3/Include/glssurfdesc.h`.
The GLS4 `SGLSpriteButton` used by the portable backend has eight states:

| Button state | GLS4 native value | GLS3 notification |
| --- | ---: | ---: |
| Normal | 0 | 0 |
| Focus | 1 | 1 |
| Checked | 2 | 2 |
| Checked and focused | 3 | 3 |
| Active | 4 | 6 |
| Checked and active | 5 | 6 |
| Disabled | 6 | 4 |
| Checked and disabled | 7 | 5 |

Forwarding the native number unchanged makes a disabled voice-replay button
look pressed to a GLS3 script. A dialogue transition can consequently display
its help banner even when the pointer is elsewhere.

The generated SDL SDK overlay translates only the status event emitted by
`SGLSpriteButton::SetButtonStatus`, and only when a parent belongs to a legacy
script tree. Legacy sprites and windows mark that boundary with
`LegacyButtonCommandTarget`. Native status, button images, native button
listeners, ordinary command notifications and standalone GLS4 trees retain
their existing values. No game IDs or status-notification parameter values
are special-cased. The supplied SDK sources remain unchanged.

`entisgls-button-status` drives real button enable/disable, hover, press/release
and check/uncheck transitions through nested parents. It covers all eight
native states, a direct legacy-window parent, unchanged native consumers and
ordinary commands, and the existing mouse/touch command-queue regressions.
The disabled-button assertion fails against the previous runtime.

For a game regression, load the first save, advance through voiced and unvoiced
dialogue, wait for speech completion, then hover and leave the replay button.
The help banner must appear only for the interaction requested by the script.
Use the host's JSON probe to capture both frames and runtime state; game
archives, saves and captured assets are local test inputs, not test fixtures
distributed with this repository.

The same dialogue regression also exercises clicks inside the message body.
The SDK's link handlers previously treated two null link pointers as a match
and dereferenced the absent hit link on both mouse down and mouse up. The SDL
overlay now requires a non-null hit link. `CheckLegacyMessage`, included in
`entisgls-graphics`, covers empty text, ordinary dialogue, a real hyperlink,
clicks outside that link and touch input after clearing it. Its empty-message
click reproduces the original segmentation fault without any game assets.
