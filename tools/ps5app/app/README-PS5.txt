OSIRIS FOR PS5
Play the Halo 5 campaign offline on a jailbroken PS5, from a game folder you build yourself on a PC from your own
Halo 5: Forge (PC) and Halo 5: Guardians (Xbox One) dumps. This app has everything it needs except that folder.

You need a jailbroken PS5 with ShadowMountPlus (to install homebrew from /data/homebrew), an FTP client on your PC,
and about 100 GB free on the console.


1. MAKE YOUR GAME FOLDER (on a Windows PC)

Run OsirisSetup.exe from the Osiris-Setup download and give it your two dumps. It makes a folder called
Halo5Campaign (about 95 GB). You can play it on the PC too: run Osiris.exe in it.


2. INSTALL THE APP

Upload the PPSA99905 folder to /data/homebrew on the PS5, so you have /data/homebrew/PPSA99905/eboot.bin.
ShadowMountPlus adds "Osiris" to the home screen.

If the app doesn't start after an FTP upload, the upload may have dropped the execute permission: set eboot.bin and
sce_module/libc.prx to 755 (most FTP clients have a "permissions" or "chmod" option).


3. OPEN IT ONCE

Start it from the home screen. The first start copies the app's Windows files into /data/halo5campaign (a few
hundred MB): the screen stays black for up to a minute. It then closes or shows an error, because there is no
game folder yet; that is expected.
(You can also skip this step and create /data/halo5campaign/Halo5Campaign yourself.)


4. COPY YOUR GAME FOLDER

Upload the contents of your Halo5Campaign folder to /data/halo5campaign/Halo5Campaign, so you end up with
  /data/halo5campaign/Halo5Campaign/halo5forge.exe
  /data/halo5campaign/Halo5Campaign/memory_release_uwp.dll
  /data/halo5campaign/Halo5Campaign/Memory_Release_UWP_343.dll
  /data/halo5campaign/Halo5Campaign/d3dcompiler_47.dll
  /data/halo5campaign/Halo5Campaign/deploy/...
and so on. It's about 95 GB; FileZilla can resume if the transfer stops.


5. PLAY

Open Osiris. Main menu > Campaign > Solo. Everything is offline; you play as "Spartan".
To quit, hold Share + L1 + R1 for 3 seconds.
Settings > Audio & Video has the frame rate (including 144 and 180) and the campaign field of view.


WHERE THINGS ARE

/data/halo5campaign/Halo5Campaign                   your game folder
/data/halo5campaign/Halo5Campaign/h5campaign        settings.ini, display.ini, saves (profile), logs
/data/halo5campaign/logs                            the app's own logs, if something goes wrong

UPDATING
New memory_release_uwp.dll or h5campaign\ForgeDesktopShim.dll files replace the ones in your game folder. Your saves
stay. The app starts halo5forge.exe (Osiris.exe is only for Windows).

REINSTALLING
The app only copies files that are missing, so to reset a broken setup delete /data/halo5campaign/prefixes and
/data/halo5campaign/profiles (keep Halo5Campaign) and open the app again.


ABOUT THIS APP
Built the same way as Installation 08's PS5 app: Prospero Win (Wine for the PS5) by mpereiraesaa and contributors,
with Wine, DXVK, vkd3d-proton (Direct3D 12), Mesa and the other parts listed in SOURCES.txt, under their own
licences (LICENSES). The changes to Prospero Win are in installation08-app.patch, built for this title.

Osiris is an unofficial fan project. It is not made, endorsed or supported by Microsoft or 343 Industries, and it
contains no game files: you need your own copies of both games. Halo is a trademark of Microsoft.
