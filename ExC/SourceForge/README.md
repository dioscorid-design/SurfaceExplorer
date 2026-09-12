# Surface Explorer 1.2

A high-performance multi-backend 4D surface visualizer built with Qt 6 and Qt RHI.

## Which file do I need?

| File | Platform |
| --- | --- |
| `SurfaceExplorer.dmg` | macOS 12.0+ (Universal) |
| `SurfaceExplorer-1.2-windows-x64.zip` | Windows 10/11, 64-bit |
| `SurfaceExplorer-v1.2-linux-x86_64.AppImage` | Linux, any x86_64 distribution |
| `install-linux.sh` | Optional, Linux only — adds the AppImage to your applications menu |

An iOS build is available on the [App Store](https://apps.apple.com/app/id6787015297).

## macOS

Open the `.dmg` and drag the app to your Applications folder. It is signed with an Apple
Developer ID and notarized, so it opens normally on first launch — no Gatekeeper workaround
needed.

## Windows

Extract the `.zip` anywhere and run `SurfaceExplorer.exe`. Everything it needs is in the
folder.

## Linux

The AppImage is self-contained: it bundles Qt and requires no installation.

**Recommended.** Download `install-linux.sh` into the same folder as the AppImage, then run:

    bash install-linux.sh

It copies the AppImage to `~/Applications`, makes it executable and adds it to your
applications menu with its icon, so from then on you launch it like any other app. Re-run it
to update, `bash install-linux.sh --uninstall` to remove. No root required.

**To run the AppImage directly instead**, make it executable first. A downloaded file carries
no permissions, and without this step your desktop may hand it to a disk-image tool instead
of launching it:

    chmod +x SurfaceExplorer-v1.2-linux-x86_64.AppImage
    ./SurfaceExplorer-v1.2-linux-x86_64.AppImage

## Requirements

* GPU with **Vulkan 1.0+**, **Metal**, **Direct3D 11+** or **OpenGL 3.3+**
* Built with **Qt 6.10.2**

## Known issues

**Windows / Direct3D.** Presets that drive the Geodesic Flow solver from a metric written in
the Equations panel may not render correctly on the Direct3D backend: the application can stop
responding and be closed by the system. They work as expected on the other platforms.

## Source and bug reports

* Source code — <https://github.com/dioscorid-design/SurfaceExplorer>
* Bug reports — <https://github.com/dioscorid-design/SurfaceExplorer/issues>
