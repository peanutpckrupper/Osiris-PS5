; Osiris (the Halo 5 campaign): the app's one game (copied to /data/halo5campaign/profiles the first time the app
; starts). The game folder is the player's own Halo5Campaign folder, made by OsirisSetup.exe on a PC.
; halo5forge.exe is started directly (Wine on the PS5 can't start a second process); the compatibility layer loads
; inside it through the folder's memory_release_uwp.dll. d3dcompiler_47.dll in the game folder is Microsoft's
; shader compiler, copied from the PC by the setup tool (Wine's can't build all of the game's runtime shaders).
[application]
id = halo5campaign
name = Osiris
executable = Z:\data\halo5campaign\Halo5Campaign\halo5forge.exe
working_directory = Z:\data\halo5campaign\Halo5Campaign
dll_overrides = d3d11,d3d12,d3d12core,dxgi=n;d3dcompiler_47=n,b
prefix = halo5campaign
runtime = wine-wow64
architecture = pe64
graphics = dxvk

[display]
desktop = 1920x1080
scaling = fit

[input]
mode = xinput
