# Learn_10 F248 native skip — standard integration

The third-cinematic skip is compiled into the **ordinary** Windows x86
`Rayman2Fix.exe`. It uses the same `SkipCinematics` option as
`jail_20`. Both skips are **enabled by default**. No research build,
environment variable or debug logging is required.

## Standard INI (generated)

```ini
[Features]
SkipCinematics=1

[Debug]
Log=0
```

The launcher also generates its other existing Graphics/Features settings.
`SkipCinematics=1` enables the native `jail_20` input skip and guarded
`learn_10` F248 skip. `SkipCinematics=0` disables both.
`Log=0` or `Log=1` does not change this behavior.

## Hidden, optional debugging override (never generated)

To disable **only `learn_10`** when debugging, manually append
`DisableLearn10Skip=1` to the existing `[Debug]` section:

```ini
[Debug]
Log=1
DisableLearn10Skip=1
```

`DisableLearn10Skip` is implicitly **0** if absent; it is **not**
inserted by the launcher into new or existing INI files. Set to 0 or
remove the line to restore `learn_10` skipping.
The original `jail_20` skip is unaffected. The old intermediate
`SkipLearn10` option is retired and cleaned from previously generated
test INIs. Do not depend on it.

## Native conditions preserved

The game calls original F248 (`GetBooleanInArray`) first. Only its
**temporary result**, for the identified director, native behaviour 3,
exact root `IfNot(F248(Fix.V42, 1188))`, first visit, and original
integer-0 result, can be changed from 0 to 1. No story progression bit
or shared AI script node is directly written. Original input and timing
guards remain native.

Function-table replacement is guarded by a verified original pointer,
native code-byte signature, writable memory and compare/exchange.
The hook is disarmed at director exit, native handover, observed timeout,
or level exit. Other F248 calls are forwarded to the native function.

## Validation

PR #34 passed Linux and Windows x86 CI and was user-validated on
Wine-staging 11.19, including visual transition, camera and controls.
Standard F248 has also been tested with both `Log=0` and `Log=1`.
This PR adds regression tests for the two main `SkipCinematics`
states and the hidden opt-out, including the generated INI's omission
of the debugging-only setting. CI is required to validate these
changes; an in-game test of the final branch remains distinct from CI.

Native Windows gameplay and exceptional teardown conditions are
not fully covered by this verification. The PR remains a draft and is
not a release without explicit authorization.
