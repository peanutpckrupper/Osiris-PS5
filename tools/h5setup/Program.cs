// OsirisSetup (was Halo5CampaignSetup): turns a player's own Halo 5: Forge (PC) dump and Halo 5: Guardians (Xbox One) dump into a
// standalone, fully offline Halo 5 campaign folder that runs on Windows or on a PS5 through prospero-win.
//
//   OsirisSetup.exe [--forge <dir>] [--guardians <dir>] [--out <dir>] [--work <dir>] [--copy] [--yes]
//
// Steps (each is safe to re-run; finished work is reused):
//   1. check the dumps        Forge build must be the supported one; Guardians must have RootFolder + sound
//   2. base game              <work>\forge = pristine Forge (hard links on the same drive, else one copy), and
//                             <out>\Halo5Campaign = hard links of that (the base the campaign is layered on)
//   3. convert the campaign   halo5onpc's prepare step (setup\worker) on <work>\forge into <work>\cache
//   4. campaign files         converted modules, variants, movie and mission lists placed at their game paths
//   5. bake                   campaign menu layer, mission lists, pause-menu residency list, merged campaign audio
//   6. launcher               Halo5Campaign.exe, the in-process loader (memory_release_uwp.dll) + h5campaign\
// Nothing is ever written into the dumps: they are only read, and linked files are always replaced, never edited.
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

const string ForgeExeSha256 = "82396C46A8811DD0D7493DB42CB22EC8938FB0CC952A6DF249D243FC0CE4CD6E";   // Halo 5: Forge 1.194.6192.2
const string ForgePackage = "Microsoft.Halo5Forge_1.194.6192.2_x64__8wekyb3d8bbwe";
var setupDir = Path.Combine(AppContext.BaseDirectory, "setup");

try { return Run(args); }
catch (SetupError e) { Fail(e.Message); return 1; }
catch (Exception e) { Fail("Unexpected error: " + e); return 1; }

int Run(string[] argv)
{
    Console.OutputEncoding = Encoding.UTF8;
    Title("Osiris setup");
    string? forge = Arg(argv, "--forge"), guardians = Arg(argv, "--guardians"), outRoot = Arg(argv, "--out"), work = Arg(argv, "--work");
    bool copy = argv.Contains("--copy"), yes = argv.Contains("--yes");
    foreach (var part in new[] { "worker", "bake", "runtime", "data" })
        if (!Directory.Exists(Path.Combine(setupDir, part))) throw new SetupError($"The setup folder is incomplete (setup\\{part} is missing). Extract the whole download and run OsirisSetup.exe from it.");

    forge ??= Ask("Folder of your Halo 5: Forge (PC) dump (the one with halo5forge.exe)");
    guardians ??= Ask("Folder of your Halo 5: Guardians (Xbox One) dump (the one with RootFolder, or RootFolder itself)");
    outRoot ??= Ask("Where should the finished game folder go? (about 100 GB)");
    forge = Full(forge); guardians = Full(guardians); outRoot = Full(outRoot);
    var game = Path.Combine(outRoot, "Halo5Campaign");
    work = Full(work ?? Path.Combine(outRoot, "Halo5Campaign-work"));

    // ---- 1. check the dumps --------------------------------------------------------------------------
    Step(1, "Checking your dumps");
    var forgeExe = Path.Combine(forge, "halo5forge.exe");
    if (!File.Exists(forgeExe)) throw new SetupError($"halo5forge.exe was not found in {forge}.");
    Info("Verifying halo5forge.exe ...");
    if (Sha256(forgeExe) != ForgeExeSha256) throw new SetupError("This halo5forge.exe is not the supported Halo 5: Forge build (1.194.6192.2). The campaign layer only works with that build.");
    if (Directory.Exists(Path.Combine(guardians, "RootFolder"))) guardians = Path.Combine(guardians, "RootFolder");
    if (!File.Exists(Path.Combine(guardians, "game_Release.exe")) || !Directory.Exists(Path.Combine(guardians, "deploy")) || !Directory.Exists(Path.Combine(guardians, "sound", "Durango")))
        throw new SetupError($"{guardians} doesn't look like a Halo 5: Guardians dump (expected game_Release.exe, deploy\\ and sound\\Durango\\ in it).");
    if (Under(game, forge) || Under(game, guardians) || Under(forge, game)) throw new SetupError("The output folder must be outside both dumps.");
    Directory.CreateDirectory(game); Directory.CreateDirectory(work);
    bool link = !copy && SameVolume(forge, game);
    long need = (link ? 70L : 105L) << 30, free = new DriveInfo(Path.GetPathRoot(game)!).AvailableFreeSpace;
    Info($"Forge:     {forge}\nGuardians: {guardians}\nOutput:    {game}\nWork:      {work}\nFiles:     {(link ? "hard links to your Forge dump where possible (no extra space for those)" : "copies")}");
    Info($"Free space on the output drive: {free >> 30} GB (setup needs roughly {need >> 30} GB more, including the work folder).");
    if (free < need && !yes && !Confirm("That may not be enough space. Continue anyway?")) return 1;

    // ---- 2. base game ---------------------------------------------------------------------------------
    // work\forge stays pristine (the conversion and the bake read it); the game folder links to it, so the campaign
    // layer can replace files in the game folder without touching either.
    Step(2, "Placing the base game files");
    var baseDir = Path.Combine(work, "forge");
    var placed = Mirror(forge, baseDir, link);
    Info($"pristine copy: {placed.Linked} linked, {placed.Copied} copied, {placed.Kept} already in place");
    placed = Mirror(baseDir, game, true);
    Info($"game folder: {placed.Linked} linked, {placed.Copied} copied, {placed.Kept} already in place");

    // ---- 3. convert the campaign ---------------------------------------------------------------------
    Step(3, "Converting the Guardians campaign (this is the long part: an hour or more)");
    var cacheParent = Path.Combine(work, "cache"); Directory.CreateDirectory(cacheParent);
    var profile = Path.Combine(work, "profile"); Directory.CreateDirectory(profile);
    // halo5onpc keeps its cache in <parent>\h5sololauncher-cache (marked by cache.json): index the dump, then prepare.
    var cache = File.Exists(Path.Combine(cacheParent, "cache.json")) ? cacheParent : Path.Combine(cacheParent, "h5sololauncher-cache");
    RunWorker(["--index", guardians, cacheParent], null, "indexing");
    if (!File.Exists(Path.Combine(cache, "cache.json"))) throw new SetupError("Indexing finished without creating the conversion cache.");
    RunWorker(["--prepare", guardians, cache, baseDir, ForgePackage], WorkerEnv(baseDir, profile), "conversion");
    CloseForge(baseDir);

    // ---- 4. campaign files ---------------------------------------------------------------------------
    Step(4, "Placing the campaign files");
    var playable = Directory.GetFiles(Path.Combine(cache, "inputs", "playable-manifests"), "*.json").OrderByDescending(File.GetLastWriteTimeUtc).FirstOrDefault()
        ?? throw new SetupError("The conversion finished without a playable manifest (work\\cache\\inputs\\playable-manifests).");
    PlaceCampaign(playable, cache, game, link);
    var art = Directory.GetFiles(Path.Combine(cache, "game", "menu-artwork"), "h5solo-menu.module", SearchOption.AllDirectories).FirstOrDefault()
        ?? throw new SetupError("The campaign menu artwork module is missing from the conversion output.");
    Place(art, Path.Combine(game, "deploy", "pc", "levels", "ui", "mainmenu", "mainmenu-rtx-12720-1.module"), link);

    // ---- 5. bake ---------------------------------------------------------------------------------------
    Step(5, "Building the campaign menu, mission lists and audio");
    var recipes = Path.Combine(cache, "inputs", "audio-extra"); Directory.CreateDirectory(recipes);
    File.Copy(Path.Combine(setupDir, "data", "extra-recipes.json"), Path.Combine(recipes, "extra-recipes.json"), overwrite: true);
    var bake = Exec(Path.Combine(setupDir, "bake", "h5bake.exe"), [cache, baseDir, game, Path.Combine(guardians, "sound", "Durango")], null, line => Info("  " + line));
    if (bake != 0) throw new SetupError($"The bake step failed (exit {bake}). See the lines above.");
    var stray = Path.Combine(game, "sound", "win", "h5solo-campaign.pck"); if (File.Exists(stray)) File.Delete(stray);   // merged into soundvoice.pck

    // ---- 6. launcher -----------------------------------------------------------------------------------
    Step(6, "Installing the launcher");
    var h5 = Path.Combine(game, "h5campaign"); Directory.CreateDirectory(h5);
    ReplaceWithCopy(Path.Combine(setupDir, "runtime", "Osiris.exe"), Path.Combine(game, "Osiris.exe"));
    ReplaceWithCopy(Path.Combine(setupDir, "runtime", "ForgeDesktopShim.dll"), Path.Combine(h5, "ForgeDesktopShim.dll"));
    // In-process loader (also how the PS5 runs it: prospero-win can't start a second process): it takes the name of
    // Forge's memory_release_uwp.dll, the first DLL halo5forge.exe loads, and forwards that library to Forge's own copy
    // kept as Memory_Release_UWP_343.dll.
    var pristineMemory = Path.Combine(baseDir, "memory_release_uwp.dll");
    if (!File.Exists(pristineMemory) || new FileInfo(pristineMemory).Length != 15872) throw new SetupError("memory_release_uwp.dll in the base game is not Forge's (re-run setup from a fresh work folder).");
    Place(pristineMemory, Path.Combine(game, "Memory_Release_UWP_343.dll"), link && SameVolume(pristineMemory, game));
    ReplaceWithCopy(Path.Combine(setupDir, "runtime", "Memory_Release_UWP.dll"), Path.Combine(game, "memory_release_uwp.dll"));
    // Microsoft's HLSL compiler, from this PC's Windows: the game compiles some shaders while it runs, and Wine's
    // compiler (used on the PS5) can't build all of them. On Windows it's the same file the game would load anyway.
    // The Windows SDK's redistributable copy ships in setup\data; this PC's own copy only if it's missing (that one needs
    // msvcp_win exports Wine doesn't have, so it doesn't work on the PS5).
    var compiler = Path.Combine(setupDir, "data", "d3dcompiler_47.dll");
    if (!File.Exists(compiler)) compiler = Path.Combine(Environment.SystemDirectory, "D3DCompiler_47.dll");
    if (File.Exists(compiler)) ReplaceWithCopy(compiler, Path.Combine(game, "d3dcompiler_47.dll"));
    else Info("  note: this PC has no D3DCompiler_47.dll; the PS5 will use Wine's shader compiler.");
    var settings = Path.Combine(h5, "settings.ini"); if (!File.Exists(settings)) File.Copy(Path.Combine(setupDir, "data", "settings.ini"), settings);
    ReplaceWithCopy(Path.Combine(setupDir, "data", "README.txt"), Path.Combine(game, "README-Halo5Campaign.txt"));
    foreach (var check in new[] { "halo5forge.exe", "Osiris.exe", "memory_release_uwp.dll", "Memory_Release_UWP_343.dll", @"h5campaign\ForgeDesktopShim.dll", @"h5campaign\ui-retain.bin", @"deploy\any\levels\globals-rtx-12720-1.module", @"sound\win\English(US)\soundvoice.pck" })
        if (!File.Exists(Path.Combine(game, check))) throw new SetupError($"The finished folder is missing {check}.");
    File.WriteAllText(Path.Combine(h5, "setup.json"), JsonSerializer.Serialize(new { Version = typeof(SetupError).Assembly.GetName().Version?.ToString(), Built = DateTime.UtcNow, Forge = ForgePackage, Linked = link }, new JsonSerializerOptions { WriteIndented = true }));

    Title("Done");
    Console.WriteLine($"Your game folder is ready:\n  {game}\n\nPlay on this PC: run Osiris.exe in that folder.\nPS5: copy the whole Halo5Campaign folder to the PS5 and open the Osiris app (it starts halo5forge.exe).\n\nThe work folder ({work}) is only needed to re-run setup. You can delete it now to free space.");
    if (!yes) { Console.WriteLine("\nPress Enter to close."); Console.ReadLine(); }
    return 0;
}

// ---- halo5onpc's conversion (worker) -----------------------------------------------------------------
Dictionary<string, string> WorkerEnv(string forgeRoot, string profile) => new()
{
    ["H5_STANDALONE_FORGE_ROOT"] = forgeRoot,
    ["H5_STANDALONE_LAUNCHER"] = Path.Combine(setupDir, "runtime", "ForgeLauncher.exe"),
    ["H5_STANDALONE_SHIM"] = Path.Combine(setupDir, "runtime", "ForgeDesktopShim.dll"),
    ["H5_STANDALONE_PROFILE"] = profile,
    ["FORGE_ENGAGE"] = "1", ["FORGE_WAM_SHAPE"] = "full", ["FORGE_AUTOSTART"] = "1",
    ["FORGE_SERVICELINK_OFFLINE"] = "1", ["FORGE_OFFLINE_ERRORS"] = "1", ["FORGE_MUTE"] = "1",
};
// The worker may leave the Forge it started for its checks running; close it.
void CloseForge(string forgeRoot)
{
    foreach (var p in Process.GetProcessesByName("halo5forge")) { try { if (p.MainModule?.FileName?.StartsWith(forgeRoot, StringComparison.OrdinalIgnoreCase) == true) { p.Kill(true); p.WaitForExit(10000); } } catch { } }
}
void RunWorker(string[] arguments, Dictionary<string, string>? env, string what)
{
    var exe = Path.Combine(setupDir, "worker", "h5sololauncher.Worker.exe");
    string? lastStage = null; JsonElement? result = null; var started = DateTime.UtcNow;
    var code = Exec(exe, arguments, env, line =>
    {
        if (!line.StartsWith('{')) { if (line.Trim().Length > 0) Info("  " + line); return; }
        try
        {
            var j = JsonDocument.Parse(line).RootElement;
            if (j.TryGetProperty("Stage", out var stage))
            {
                var name = stage.GetString(); int done = j.TryGetProperty("Completed", out var c) ? c.GetInt32() : 0, total = j.TryGetProperty("Total", out var t) ? t.GetInt32() : 0;
                var text = total > 0 ? $"{name} ({done * 100L / Math.Max(1, total)}%)" : name;
                if (text != lastStage) { Info($"  [{(DateTime.UtcNow - started):hh\\:mm\\:ss}] {text}"); lastStage = text; }
            }
            else if (j.TryGetProperty("State", out _)) result = j.Clone();
        }
        catch (JsonException) { Info("  " + line); }
    });
    var state = result?.GetProperty("State").GetString();
    if (code != 0 || state is "Failed" or null)
    {
        var msg = result?.TryGetProperty("Message", out var m) == true ? m.GetString() : null;
        throw new SetupError($"The campaign {what} failed (exit {code}{(state is null ? "" : ", " + state)}). {msg}");
    }
    var done2 = result?.TryGetProperty("Message", out var dm) == true ? dm.GetString() : state;
    Info($"  {what} finished ({(DateTime.UtcNow - started):hh\\:mm\\:ss}): {done2}");
}

// ---- file placement ------------------------------------------------------------------------------------
(int Linked, int Copied, int Kept) Mirror(string from, string to, bool link)
{
    int linked = 0, copied = 0, kept = 0, n = 0;
    // Not part of the game: halo5onpc drops its runtime helper DLLs in <forge>\campaign (the conversion step does too),
    // and h5campaign\ is this launcher's own folder.
    foreach (var skip in new[] { "campaign", "h5campaign" }) { var stale = Path.Combine(to, skip); if (skip == "campaign" && Directory.Exists(stale)) Directory.Delete(stale, true); }
    foreach (var file in Directory.EnumerateFiles(from, "*", SearchOption.AllDirectories))
    {
        var rel = Path.GetRelativePath(from, file); var target = Path.Combine(to, rel);
        var top = rel.Split('\\')[0];
        if (rel.Contains('\\') && (top.Equals("campaign", StringComparison.OrdinalIgnoreCase) || top.Equals("h5campaign", StringComparison.OrdinalIgnoreCase))) continue;
        if (++n % 500 == 0) Info($"  {n} files ...");
        if (File.Exists(target)) { kept++; continue; }   // complete (copies land via a temp name); may be a campaign file
        if (Place(file, target, link)) linked++; else copied++;
    }
    return (linked, copied, kept);
}
void PlaceCampaign(string manifestPath, string cache, string game, bool link)
{
    using var doc = JsonDocument.Parse(File.ReadAllBytes(manifestPath)); var root = doc.RootElement;
    int files = 0, maps = 0;
    var entries = root.GetProperty("Files").EnumerateArray().Select(f => (f.GetProperty("OriginalPath").GetString()!, f.GetProperty("CachePath").GetString()!)).ToList();
    if (root.TryGetProperty("Movie", out var movie) && movie.ValueKind == JsonValueKind.Object) entries.Add((movie.GetProperty("OriginalPath").GetString()!, movie.GetProperty("CachePath").GetString()!));
    foreach (var (original, cached) in entries)
    {
        if (original.EndsWith("h5solo-campaign.pck", StringComparison.OrdinalIgnoreCase)) continue;   // the bake merges it into soundvoice.pck
        var source = Path.Combine(cache, cached.Replace('/', '\\')); var target = Path.Combine(game, original.Replace('/', '\\'));
        if (!File.Exists(source)) throw new SetupError($"A converted file is missing from the cache: {cached}");
        var have = new FileInfo(target); if (have.Exists && have.Length == new FileInfo(source).Length && SameFile(source, target)) continue;
        Place(source, target, link && SameVolume(source, game)); if (++files % 100 == 0) Info($"  {files} campaign files ...");
    }
    // Mission lists: halo5onpc routes each converted mission's module list to Forge's globals layers at launch; the same
    // rewrite is stored in the file (Bond list<string>, field header CB 07 09).
    foreach (var map in root.GetProperty("Maps").EnumerateArray())
    {
        var scenario = map.GetProperty("Scenario").GetString()!; var sha = map.GetProperty("MetadataSha256").GetString()!.ToLowerInvariant();
        var source = Path.Combine(cache, "game", "metadata", sha + ".mapinfo");
        if (!File.Exists(source)) throw new SetupError($"Mission list for {scenario} is missing from the cache.");
        var target = Path.Combine(game, "__cms__", "rtx", scenario.Replace('/', '\\') + ".mapinfo");
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        if (File.Exists(target)) File.Delete(target);
        File.WriteAllBytes(target, MapInfoRoute(File.ReadAllBytes(source))); maps++;
    }
    Info($"  {files} campaign files placed, {maps} mission lists written");
}
static byte[] MapInfoRoute(byte[] d)
{
    string[] forgeGlobals = [@"any\levels\globals-rtx-1.module", @"%(Platform)\levels\globals-rtx-1.module", @"any\levels\globals-rtx-11587-1.module", @"%(Platform)\levels\globals-rtx-11587-1.module",
        @"any\levels\globals-rtx-12719-1.module", @"%(Platform)\levels\globals-rtx-12719-1.module", @"%(Platform)\levels\globals-rtx-12720-1.module"];
    byte[] marker = [0xCB, 0x07, 0x09];
    var at = d.AsSpan().IndexOf(marker); if (at < 0 || d.AsSpan(at + 1).IndexOf(marker) >= 0) throw new SetupError("Unexpected mission list format.");
    int i = at + 3; var count = Varint(d, ref i); var items = new List<string>();
    for (var k = 0; k < count; k++) { var n = Varint(d, ref i); items.Add(Encoding.ASCII.GetString(d, i, n)); i += n; }
    var list = forgeGlobals.Concat(items.Where(s => !s.Contains(@"\levels\globals-rtx-"))).ToList();
    using var o = new MemoryStream(); o.Write(d, 0, at + 3); WriteVarint(o, list.Count);
    foreach (var s in list) { WriteVarint(o, s.Length); o.Write(Encoding.ASCII.GetBytes(s)); }
    o.Write(d, i, d.Length - i); return o.ToArray();
    static int Varint(byte[] b, ref int p) { int v = 0, s = 0; while (true) { var x = b[p++]; v |= (x & 0x7f) << s; s += 7; if ((x & 0x80) == 0) return v; } }
    static void WriteVarint(Stream st, int v) { do { var x = (byte)(v & 0x7f); v >>= 7; st.WriteByte((byte)(x | (v != 0 ? 0x80 : 0))); } while (v != 0); }
}
// Places one file: a hard link when allowed (no extra space; the target is always replaced, never edited), else a copy.
bool Place(string source, string target, bool link)
{
    Directory.CreateDirectory(Path.GetDirectoryName(target)!);
    if (File.Exists(target)) File.Delete(target);
    if (link && CreateHardLinkW(target, source, IntPtr.Zero)) return true;
    var temp = target + ".h5part"; File.Copy(source, temp, overwrite: true); File.Move(temp, target, overwrite: true);
    return false;
}
static void ReplaceWithCopy(string source, string target) { Directory.CreateDirectory(Path.GetDirectoryName(target)!); if (File.Exists(target)) File.Delete(target); File.Copy(source, target); }
static bool SameFile(string a, string b) => FileId(a) is { } x && FileId(b) is { } y && x == y;
static (uint, ulong)? FileId(string path)
{
    using var h = File.OpenHandle(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
    return GetFileInformationByHandle(h.DangerousGetHandle(), out var info) ? (info.VolumeSerialNumber, ((ulong)info.FileIndexHigh << 32) | info.FileIndexLow) : null;
}
static bool SameVolume(string a, string b) => string.Equals(Path.GetPathRoot(Full(a)), Path.GetPathRoot(Full(b)), StringComparison.OrdinalIgnoreCase);
static bool Under(string path, string folder) { var p = Full(path).TrimEnd('\\') + "\\"; var f = Full(folder).TrimEnd('\\') + "\\"; return p.StartsWith(f, StringComparison.OrdinalIgnoreCase); }

// ---- process / console helpers -------------------------------------------------------------------------
int Exec(string exe, string[] arguments, Dictionary<string, string>? env, Action<string> onLine)
{
    if (!File.Exists(exe)) throw new SetupError($"Missing setup component: {exe}");
    var psi = new ProcessStartInfo(exe) { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true, WorkingDirectory = Path.GetDirectoryName(exe)! };
    foreach (var a in arguments) psi.ArgumentList.Add(a);
    if (env is not null) foreach (var (k, v) in env) psi.Environment[k] = v;
    using var p = Process.Start(psi)!;
    p.OutputDataReceived += (_, e) => { if (e.Data is not null) onLine(e.Data); };
    p.ErrorDataReceived += (_, e) => { if (e.Data is not null) onLine(e.Data); };
    p.BeginOutputReadLine(); p.BeginErrorReadLine();
    // Wait for the process itself, not for end-of-output: a Forge the worker started inherits its stdout and keeps the
    // pipe open after the worker has exited.
    while (!p.WaitForExit(1000)) { }
    p.WaitForExit(5000);
    return p.ExitCode;
}
static string Sha256(string path) { using var s = File.OpenRead(path); return Convert.ToHexString(SHA256.HashData(s)); }
static string Full(string p) => Path.GetFullPath(p.Trim().Trim('"'));
static string? Arg(string[] a, string name) { var i = Array.IndexOf(a, name); return i >= 0 && i + 1 < a.Length ? a[i + 1] : null; }
static string Ask(string prompt) { while (true) { Console.Write(prompt + ":\n> "); var s = Console.ReadLine()?.Trim().Trim('"'); if (!string.IsNullOrEmpty(s)) return s; } }
static bool Confirm(string q) { Console.Write(q + " [y/N] "); return Console.ReadLine()?.Trim().StartsWith("y", StringComparison.OrdinalIgnoreCase) == true; }
static void Title(string t) { Console.WriteLine(); Console.WriteLine(t); Console.WriteLine(new string('=', t.Length)); }
static void Step(int n, string t) { Console.WriteLine(); Console.ForegroundColor = ConsoleColor.Cyan; Console.WriteLine($"[{n}/6] {t}"); Console.ResetColor(); }
static void Info(string t) => Console.WriteLine(t);
static void Fail(string t) { Console.ForegroundColor = ConsoleColor.Red; Console.WriteLine("\nSetup stopped: " + t); Console.ResetColor(); Console.WriteLine("Fix the problem and run setup again; finished work is reused.\nPress Enter to close."); try { if (!Console.IsInputRedirected) Console.ReadLine(); } catch { } }

[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateHardLinkW(string newFile, string existing, IntPtr security);
[DllImport("kernel32.dll", SetLastError = true)] static extern bool GetFileInformationByHandle(IntPtr file, out ByHandleFileInformation info);

[StructLayout(LayoutKind.Sequential)]
struct ByHandleFileInformation
{
    public uint FileAttributes; public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime, LastAccessTime, LastWriteTime;
    public uint VolumeSerialNumber, FileSizeHigh, FileSizeLow, NumberOfLinks, FileIndexHigh, FileIndexLow;
}
sealed class SetupError(string message) : Exception(message);
