# Musique Dynamics

Musique Dynamics is a Windows dynamics processor for controlling level, punch and transient shape. It is available as a Standalone application and a VST3 plug-in.

## Formats

- Windows x64 Standalone
- Windows x64 VST3

## Install a release

1. Download the Windows installer or portable ZIP from this repository's Releases page.
2. Run the installer, or extract the ZIP and copy the complete .vst3 bundle to a VST3 location scanned by your host.
3. Rescan plug-ins in the host, then insert the effect on the track or bus you want to process.

## Processing modes

Musique Dynamics covers the main dynamics workflows:

- Compression for vocal, drum, bass and bus control.
- Limiting for fast safety, broadcast-style control and loudness management.
- Gate and expander modes for cleanup and room control.
- Multiband processing for frequency-dependent compression.
- Transient shaping for attack and sustain adjustment.

The active mode presents controls relevant to that task. Use Mix for parallel processing and Output for level matching when comparing settings.

## Factory presets

The 18 factory presets include Vocal Glue, Drum Smash, Master Gentle, Bass Control, limiter, gate, expander, multiband and transient-shaping starts. They are intended as musical starting points, not fixed mastering settings.

## Build from source

Requirements: Windows x64, PowerShell, Git, CMake 3.22 or later, Visual Studio 2022 (or Build Tools) with Desktop development with C++, and JUCE 8.0.4.

~~~powershell
.\_build_all.ps1 -Configuration Release -BootstrapJuce
~~~

To use an existing JUCE 8.0.4 checkout:

~~~powershell
.\_build_all.ps1 -Configuration Release -JuceDir C:\Dev\JUCE
~~~

The build produces Standalone and VST3 artefacts.

## Package a local build

~~~powershell
.\_package_release.ps1 -Configuration Release -BootstrapJuce
~~~

The script creates a portable Windows package and, when Inno Setup 6 is installed, a Windows installer. Use the SkipInstaller option when an installer is not required.

## Repository contents

| Path | Purpose |
| --- | --- |
| Source/ | Plug-in source, effect engines and visual assets |
| Presets/ | Factory preset bank |
| FXShared/ | Local shared UI and audio helpers required by this plug-in |
| installer/ | Windows installer definition |

## Licence and support

This project is source-available, not open source. See [LICENSE.md](LICENSE.md) for the permitted use of source and binaries. For a released-build issue, open an issue with the Windows version, host name/version, plug-in format and steps to reproduce it.
