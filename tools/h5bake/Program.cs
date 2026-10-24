// h5bake: writes halo5onpc's campaign menu into standalone Forge's files (no runtime patching).
// Builds deploy\any\levels\globals-rtx-12720-1.module holding campaign versions of the 7 menu tags halo5onpc
// replaces at runtime (main menu lua/wpf, theater lobby lua/wpf + background, campaign lobby strings), using the
// same edits as halo5onpc's MenuConfiguration, then adds that layer (and the artwork module, linked as
// %(Platform)\levels\ui\mainmenu\mainmenu-rtx-12720-1.module by the runner) to the mapinfo module lists.
// Usage: h5bake <cacheRoot> <forgeRoot (vanilla Dev\Forge)> <outputRoot (Dev\ForgeCampaign)>
using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
using H5SoloLauncher.Core.Content;
using H5SoloLauncher.Core.Conversion;
using H5SoloLauncher.Core.Runtime;

// h5bake --display <gameFolder>: add/refresh the display options settings script (and the main menu's Start -> Spartan
// Hub binding) in an already-baked game folder's campaign layer (deploy\any\levels\globals-rtx-12720-1.module); everything else in the layer is kept as is.
if (args.Length == 2 && args[0] == "--display")
{
    var game = args[1]; var layerPath = Path.Combine(game, "deploy", "any", "levels", "globals-rtx-12720-1.module");
    if (!File.Exists(layerPath)) { Console.Error.WriteLine("no campaign layer in " + game); return 2; }
    var display = DisplayEntry(game);
    var meta = FileMetadata.Read(layerPath, "Module", default); var kept = new List<(byte[] Raw, string Name, byte[] Payload)>();
    using (var ls = File.OpenRead(layerPath))
        for (var i = 0; i < meta.ItemCount; i++)
        {
            var e = meta.Entry(i); if (e.Group == "luas" && e.TagId == "003d87f1") continue;
            if (e.ResourceCount != 0 || e.Parent != -1) throw new InvalidOperationException("unexpected resources in the campaign layer");
            var bytes = ModulePayloadReader.Read(ls, meta, e, default).Bytes;
            if (e.Group == "wpfs" && e.TagId == "00024a4e") bytes = MainMenuStart(bytes);   // main menu: Start opens the Spartan Hub
            kept.Add((e.Raw, e.Name, bytes));
        }
    kept.Add(display);
    using var names = new MemoryStream(); var list = new List<ModuleWriteEntry>();
    foreach (var (raw0, name, payload) in kept)
    {
        var raw = (byte[])raw0.Clone(); BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(0), (int)names.Length); BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(12), 0);
        names.Write(Encoding.UTF8.GetBytes(name + '\0')); list.Add(new(raw, () => new ModuleWritePayload(payload, RehashAsset: true)));
    }
    var temp = layerPath + ".h5tmp"; if (File.Exists(temp)) File.Delete(temp);
    var done = ModuleWriter.Write(temp, new ModuleWriteLayout(meta.Header, list.ToArray(), names.ToArray(), [], list.Count));
    File.Move(temp, layerPath, overwrite: true);
    Console.WriteLine($"display options: {layerPath} rewritten, {done.Entries} tags, {done.Bytes} bytes");
    return 0;
}
// h5bake --dump <module> <outDir> [nameFilter]: write each entry's payload (and index.txt) for inspection.
if (args.Length is 3 or 4 && args[0] == "--dump")
{
    var meta = FileMetadata.Read(args[1], "Module", default); Directory.CreateDirectory(args[2]);
    using var ls = File.OpenRead(args[1]); var index = new List<string>();
    for (var i = 0; i < meta.ItemCount; i++)
    {
        var e = meta.Entry(i); index.Add($"{i}\t{e.Group}\t{e.TagId}\t{e.Parent}\t{e.ResourceCount}\t{e.Name}");
        if (args.Length == 4 && !e.Name.Contains(args[3], StringComparison.OrdinalIgnoreCase)) continue;
        if (e.Parent != -1) continue;
        try { File.WriteAllBytes(Path.Combine(args[2], $"{e.Group}-{e.TagId}-{i}.bin"), ModulePayloadReader.Read(ls, meta, e, default).Bytes); }
        catch (Exception x) { index.Add("  failed: " + x.Message); }
    }
    File.WriteAllLines(Path.Combine(args[2], "index.txt"), index); Console.WriteLine($"{meta.ItemCount} entries"); return 0;
}
if (args.Length is not (3 or 4)) { Console.Error.WriteLine("usage: h5bake <cacheRoot> <forgeRoot> <outputRoot> [<guardiansSoundRoot>] | --display <gameFolder>"); return 2; }
var (cache, forge, output) = (args[0], args[1], args[2]);
// Guardians' sound\Durango (source of the extra soundbanks); the setup tool passes the player's own dump.
var guardiansSound = args.Length == 4 ? args[3] : @"C:\Games\Halo5-Guardians-Vanilla\RootFolder\sound\Durango";

// ---- menu sources (from halo5onpc's prepare step) ------------------------------------------------
var manifestPath = Directory.GetFiles(Path.Combine(cache, "inputs", "menu-source-manifests"), "*.json").OrderByDescending(File.GetLastWriteTimeUtc).First();
using var manifest = JsonDocument.Parse(File.ReadAllBytes(manifestPath));
byte[] Payload(JsonElement proof) => File.ReadAllBytes(Path.Combine(cache, "inputs", "menu-payloads", proof.GetProperty("PayloadSha256").GetString()!.ToLowerInvariant() + ".bin"));
var tags = manifest.RootElement.GetProperty("Tags").EnumerateArray().ToDictionary(t => t.GetProperty("Role").GetString()!);
var artworkPath = Directory.GetFiles(Path.Combine(cache, "inputs", "menu-artwork-manifests"), "*.json").OrderByDescending(File.GetLastWriteTimeUtc).First();
using var artworkManifest = JsonDocument.Parse(File.ReadAllBytes(artworkPath));
var artwork = artworkManifest.RootElement.GetProperty("Tags").EnumerateArray().ToDictionary(t => t.GetProperty("Identity").GetProperty("Group").GetString() + ":" + t.GetProperty("Identity").GetProperty("TagId").GetString(), t => t.GetProperty("Identity"));

// The Guardians tags reference Guardians assets. halo5onpc rebinds each reference in memory to the Forge tag (or
// campaign artwork) that actually exists; here the same targets are written into the tag file: the reference
// field (gid +12, asset +16, group +24) and its dependency entry (group +0, asset +8, gid +16). The 5 optional
// lobby sounds Forge lacks become null references.
byte[] Rebind(byte[] payload, JsonElement item)
{
    var doc = new TagDocument(payload, resource: true); var bytes = (byte[])payload.Clone(); int rebound = 0, nulled = 0;
    foreach (var binding in item.GetProperty("Bindings").EnumerateArray())
    {
        int block = binding.GetProperty("Block").GetInt32(), offset = binding.GetProperty("Offset").GetInt32();
        var reference = doc.References.Single(r => r.FieldBlock == block && r.FieldOffset == offset);
        var source = binding.GetProperty("Source"); var key = source.GetProperty("Group").GetString() + ":" + source.GetProperty("TagId").GetString();
        JsonElement? target = binding.GetProperty("Target").ValueKind == JsonValueKind.Object ? binding.GetProperty("Target").GetProperty("Identity") : artwork.TryGetValue(key, out var art) ? art : null;
        var field = doc.BlockOffset(block) + offset; var dep = reference.Dependency >= 0 ? 80 + reference.Dependency * 24 : -1;
        if (target is null)
        {
            if (source.GetProperty("Group").GetString() != "snd!") throw new InvalidOperationException("unresolved non-sound menu reference " + key);
            BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(field + 12), 0xffffffff); BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(field + 16), 0);
            nulled++; continue;
        }
        var group = Encoding.ASCII.GetBytes(target.Value.GetProperty("Group").GetString()!); Array.Reverse(group);
        var gid = uint.Parse(target.Value.GetProperty("TagId").GetString()!, System.Globalization.NumberStyles.HexNumber);
        var asset = ulong.Parse(target.Value.GetProperty("AssetId").GetString()!, System.Globalization.NumberStyles.HexNumber);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(field + 12), gid); BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(field + 16), asset); group.CopyTo(bytes, field + 24);
        if (dep >= 0) { group.CopyTo(bytes, dep); BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(dep + 8), asset); BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(dep + 16), gid); }
        rebound++;
    }
    Console.WriteLine($"  {item.GetProperty("Role").GetString()}: {rebound} references rebound, {nulled} optional sounds nulled");
    return bytes;
}

// ---- display options settings script --------------------------------------------------------------
// Forge's audio and video settings script (luas 003d87f1) with halo5onpc's display-ui recipe applied: adds the 144/180
// FPS frame rate entries and the campaign field of view slider that the shim's display options implement.
// The Guardians main menu opens the Spartan Hub on Start through its own menu graph; under Forge's graph nothing
// answers Start there (Forge reaches its hub from a Settings entry instead). Requesting the graph's show_spartan_hub
// transition directly freezes input (the hub is driven by the engine's hub state machine, not by that transition), so
// Start calls the engine entry Forge's own main menu uses, Anubis.NodeGraph.DirectLinkSpartanHubSettings*() (opens the
// hub through that state machine at its settings pages: Gameplay, Controller, Mouse/Keyboard, Audio/Video). B closes
// it and the main menu has focus again (tested 2026-10-23); no refocus handler is needed.
// Rebuilt on every run: earlier forms of these edits are taken out first.
// Development overrides: H5BAKE_HUB_OPEN (LuaLine for Start), H5BAKE_HUB_REFOCUS (extra LuaLine run on load).
static byte[] MainMenuStart(byte[] payload)
{
    var doc = new TagDocument(payload, resource: true); var r = DataAt(doc, 160);
    var data = doc.Block(r.Target).ToArray(); var original = data;
    bool Has(string text) => data.AsSpan().IndexOf(Encoding.UTF8.GetBytes(text)) >= 0;
    foreach (var earlier in new[] {
        "<halo:DoLuaAction LuaLine=\"Hui.RegisterEvent([[spartan_hub_closed]], [[UIShow]])\"/>",
        "<halo:HaloSoundPlayerAction x:Name=\"H5HubSound\" Name=\"Sound_StartPressed\" Source=\"tagsound://Gx0005fb95\" halo:HaloConditional.FilterKeys=\"Start\"/>",
        "<halo:HaloSoundPlayerAction Name=\"Sound_StartPressed\" Source=\"tagsound://Gx0005fb95\" halo:HaloConditional.FilterKeys=\"Start\"/>",
        "<halo:NodeTransitionRequestAction TransitionEventName=\"show_spartan_hub\" halo:HaloConditional.FilterKeys=\"Start\"/>" })
        if (Has(earlier)) data = XmlChange(data, earlier, "");
    foreach (var name in new[] { "H5HubRefocus", "H5HubOpen" })
        for (int at; (at = data.AsSpan().IndexOf(Encoding.UTF8.GetBytes("<halo:DoLuaAction x:Name=\"" + name + "\""))) >= 0;)
        { var end = data.AsSpan(at).IndexOf("/>"u8) + 2; data = [.. data.AsSpan(0, at), .. data.AsSpan(at + end)]; }
    static string Esc(string v) => System.Security.SecurityElement.Escape(v)!;
    var open = Environment.GetEnvironmentVariable("H5BAKE_HUB_OPEN") ?? "Anubis.NodeGraph.DirectLinkSpartanHubSettingsGameplay()";
    const string y = "<halo:DoLuaAction LuaLine=\"OnYButtonPress()\" halo:HaloConditional.FilterKeys=\"Y\"/>";
    data = XmlChange(data, y, y +
        "<halo:HaloSoundPlayerAction Name=\"Sound_StartPressed\" Source=\"tagsound://Gx0005fb95\" halo:HaloConditional.FilterKeys=\"Start\"/>" +
        "<halo:DoLuaAction x:Name=\"H5HubOpen\" LuaLine=\"" + Esc(open) + "\" halo:HaloConditional.FilterKeys=\"Start\"/>");
    var refocus = Environment.GetEnvironmentVariable("H5BAKE_HUB_REFOCUS");
    if (!string.IsNullOrEmpty(refocus) && refocus != "none")
        data = XmlChange(data, "<halo:DoLuaAction LuaLine=\"OnLoaded()\"/>", "<halo:DoLuaAction LuaLine=\"OnLoaded()\"/><halo:DoLuaAction x:Name=\"H5HubRefocus\" LuaLine=\"" + Esc(refocus) + "\"/>");
    if (data.AsSpan().SequenceEqual(original)) return payload;
    return Rebuild(doc, new() { [r.Target] = data }, root => BinaryPrimitives.WriteUInt32LittleEndian(root.AsSpan(160 + 24), (uint)data.Length));
}
static (byte[] Raw, string Name, byte[] Payload) DisplayEntry(string forgeRoot)
{
    (FileMetadata Meta, ModuleEntry Entry, byte[] Bytes)? found = null;
    foreach (var name in new[] { "globals-rtx-1.module", "globals-rtx-11587-1.module", "globals-rtx-12719-1.module" })   // ranked: the last one wins
        foreach (var platform in new[] { "any", "pc" })
        {
            var path = Path.Combine(forgeRoot, "deploy", platform, "levels", name); if (!File.Exists(path)) continue;
            var meta = FileMetadata.Read(path, "Module", default); using var stream = File.OpenRead(path);
            for (var i = 0; i < meta.ItemCount; i++) { var e = meta.Entry(i); if (e.Group == "luas" && e.TagId == "003d87f1" && e.StoredSize > 0) found = (meta, e, ModulePayloadReader.Read(stream, meta, e, default).Bytes); }
        }
    if (found is null) throw new InvalidOperationException("Forge's audio and video settings script (luas 003d87f1) was not found");
    var (_, entry, bytes) = found.Value;
    if (entry.ResourceCount != 0) throw new InvalidOperationException("the settings script has resources");
    var doc = new TagDocument(bytes, resource: true); var reference = doc.DataReferences.Single(x => x.FieldBlock == 0 && x.FieldOffset == 20);
    var patched = DisplayConfiguration.PatchUi(doc.Block(reference.Target).ToArray());
    var result = Rebuild(doc, new() { [reference.Target] = patched }, root => BinaryPrimitives.WriteUInt32LittleEndian(root.AsSpan(20 + 24), (uint)patched.Length));
    Console.WriteLine($"  DisplayLua     {entry.Name}: {bytes.Length} -> {result.Length} bytes");
    return (entry.Raw, entry.Name, result);
}

// ---- tag payload editing -------------------------------------------------------------------------
// Rebuilds a ucsh payload with replaced section-one blocks: block sizes/offsets and the data section size
// are recomputed (original gaps kept); header tables and the resource section are unchanged.
static byte[] Rebuild(TagDocument doc, Dictionary<int, byte[]> replaced, Action<byte[]>? patchRoot = null)
{
    var blocks = Enumerable.Range(0, doc.Blocks.Length).Select(i => replaced.TryGetValue(i, out var b) ? b : doc.Block(i).ToArray()).ToArray();
    if (replaced.Keys.Any(i => doc.Blocks[i].Section != 1)) throw new InvalidOperationException("only data-section blocks can be replaced");
    patchRoot?.Invoke(blocks[0]);
    var header = doc.Bytes.AsSpan(0, doc.HeaderSize).ToArray();
    var order = Enumerable.Range(0, blocks.Length).Where(i => doc.Blocks[i].Section == 1).OrderBy(i => doc.Blocks[i].Offset).ToArray();
    using var data = new MemoryStream(); long previousEnd = 0;
    foreach (var i in order)
    {
        var gap = checked((long)doc.Blocks[i].Offset - previousEnd); if (gap < 0) throw new InvalidOperationException("overlapping blocks");
        previousEnd = (long)doc.Blocks[i].Offset + doc.Blocks[i].Size;
        data.Write(new byte[gap]);
        var at = doc.Blocks[i].TableOffset;
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(at), (uint)blocks[i].Length);
        BinaryPrimitives.WriteUInt64LittleEndian(header.AsSpan(at + 8), (ulong)data.Length);
        data.Write(blocks[i]);
    }
    data.Write(new byte[Math.Max(0, doc.DataSize - previousEnd)]);   // trailing data-section padding
    BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(64), (uint)data.Length);
    var resources = doc.Bytes.AsSpan(doc.HeaderSize + doc.DataSize).ToArray();
    var result = header.Concat(data.ToArray()).Concat(resources).ToArray();
    _ = new TagDocument(result, resource: true);   // must parse back
    return result;
}
static byte[] ReplaceOnce(byte[] bytes, byte[] from, byte[] to)
{
    var at = bytes.AsSpan().IndexOf(from);
    if (at < 0 || bytes.AsSpan(at + from.Length).IndexOf(from) >= 0) throw new InvalidOperationException("edit anchor missing or ambiguous");
    return [.. bytes.AsSpan(0, at), .. to, .. bytes.AsSpan(at + from.Length)];
}
static byte[] XmlChange(byte[] bytes, string from, string to)
{
    var result = ReplaceOnce(bytes, Encoding.UTF8.GetBytes(from), Encoding.UTF8.GetBytes(to));
    _ = XDocument.Parse(new UTF8Encoding(false, true).GetString(result).TrimEnd('\0').TrimStart('﻿'), LoadOptions.PreserveWhitespace);
    return result;
}
static byte[] Constant(string value) { var b = Encoding.ASCII.GetBytes(value + '\0'); var o = new byte[8 + b.Length]; BinaryPrimitives.WriteUInt64BigEndian(o, (ulong)b.Length); b.CopyTo(o, 8); return o; }
static uint NameId(string name)
{
    var s = Encoding.ASCII.GetBytes(name); uint h = 0;
    static uint Rot(uint n, int b) => (n << b) | (n >> (32 - b));
    for (var i = 0; i < s.Length; i += 4)
    {
        var count = Math.Min(4, s.Length - i); uint k = 0; for (var j = 0; j < count; j++) k |= (uint)s[i + j] << (8 * j);
        k *= 0xcc9e2d51; k = Rot(k, 15) * 0x1b873593; h ^= k; if (count == 4) h = Rot(h, 13) * 5 + 0xe6546b64;
    }
    h ^= (uint)s.Length; h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16; return h;
}
static TagDataReference DataAt(TagDocument d, int offset) => d.DataReferences.Single(x => x.FieldBlock == 0 && x.FieldOffset == offset);
static byte[] WithSchemaOf(byte[] payload, TagDocument native) { var o = (byte[])payload.Clone(); native.Bytes.AsSpan(8, 8).CopyTo(o.AsSpan(8)); return o; }

var built = new Dictionary<string, byte[]>();
foreach (var role in new[] { "MainLua", "MainWpf", "CampaignLua", "CampaignWpf", "SkullStrings", "MenuGraph", "BackgroundLua", "BackgroundWpf" })
{
    var item = tags[role]; var native = new TagDocument(Payload(item.GetProperty("Native")), resource: true);
    var hasSource = item.TryGetProperty("Source", out var src) && src.ValueKind == JsonValueKind.Object;
    var candidate = hasSource ? new TagDocument(Payload(src), resource: true) : native;
    byte[] result;
    switch (role)
    {
        case "MainLua": case "MainWpf":
        {
            // Native tag; only its script/XAML data block becomes the Guardians version (size at field+24).
            var offset = role == "MainLua" ? 20 : 160;
            var mine = DataAt(native, offset); var theirs = DataAt(candidate, offset);
            var data = candidate.Block(theirs.Target).ToArray();
            // Signed in for real, the game counts as online and the Guardians menu takes the online campaign lobby
            // (343's lobby service, which refuses this copy). Ask "IsOffline" where it asks "IsOnline" so it takes
            // its own offline campaign path (k_OfflineCampaign), as halo5onpc's Solo entry forced.
            // Opt-in only: tested 2026-10-22, the main menu list then failed to load (IsOffline is not callable there).
            if (role == "MainLua" && Environment.GetEnvironmentVariable("H5BAKE_OFFLINE_EDIT") is not null) data = ReplaceOnce(data, Constant("IsOnline"), Constant("IsOffline"));
            // halo5onpc's automatic Solo entry, made permanent but once per session: the first time the main menu
            // loads, open the offline campaign lobby (k_OfflineCampaign). Pressing Solo by hand doesn't open it in Forge.
            var soloOnce = Environment.GetEnvironmentVariable("H5BAKE_SOLO_GUARD") is null
                ? "Anubis.Lobby.RequestGotoLobbyActivity(Hui.Import(Anubis.Lobby.eLobbyMenuActivity).k_OfflineCampaign, true)"   // halo5onpc's exact line
                : "if not h5_offline_solo_opened then h5_offline_solo_opened = true; Anubis.Lobby.RequestGotoLobbyActivity(Hui.Import(Anubis.Lobby.eLobbyMenuActivity).k_OfflineCampaign, true) end";   // guarded form did not fire
            if (role == "MainWpf") data = XmlChange(data, "<halo:DoLuaAction LuaLine=\"OnLoaded()\"/>", "<halo:DoLuaAction LuaLine=\"OnLoaded()\"/><halo:DoLuaAction LuaLine=\"OnListBoxSelectionChange()\"/>" +
                (Environment.GetEnvironmentVariable("H5BAKE_AUTOSOLO") is not null ? "<halo:DoLuaAction LuaLine=\"" + soloOnce + "\"/>" : ""));
            result = Rebuild(native, new() { [mine.Target] = data }, root => BinaryPrimitives.WriteUInt32LittleEndian(root.AsSpan(offset + 24), (uint)data.Length));
            if (role == "MainWpf") result = MainMenuStart(result);
            break;
        }
        case "CampaignLua":
        {
            var r = DataAt(candidate, 20); var data = ReplaceOnce(candidate.Block(r.Target).ToArray(), Constant("goto_campaign_background"), Constant("goto_theater"));
            result = WithSchemaOf(Rebuild(candidate, new() { [r.Target] = data }, root => BinaryPrimitives.WriteUInt32LittleEndian(root.AsSpan(20 + 24), (uint)data.Length)), native);
            break;
        }
        case "CampaignWpf":
        {
            var r = DataAt(candidate, 160); var data = XmlChange(candidate.Block(r.Target).ToArray(), "Text=\"{halo:Loc found}\"", "Text=\"\"");
            result = WithSchemaOf(Rebuild(candidate, new() { [r.Target] = data }, root => BinaryPrimitives.WriteUInt32LittleEndian(root.AsSpan(160 + 24), (uint)data.Length)), native);
            break;
        }
        case "SkullStrings":
        {
            var merged = MenuConfiguration.MergeStrings(native, candidate);
            result = Rebuild(native, new() { [1] = merged.Records, [2] = merged.Text }, root =>
            {
                BinaryPrimitives.WriteInt32LittleEndian(root.AsSpan(32), merged.Records.Length / 72);
                BinaryPrimitives.WriteUInt32LittleEndian(root.AsSpan(72 + 24), (uint)merged.Text.Length);
            });
            break;
        }
        case "MenuGraph":
        {
            // Forge's menu graph has no route for the offline campaign: in its "any" node the transition
            // goto_theater -> theater_lobby is renamed to goto_campaign_solo (the event the Solo entry raises), the
            // same 4-byte change halo5onpc makes in memory. String ids are murmur3 hashes.
            var from = BitConverter.GetBytes(NameId("goto_theater")); var lobby = BitConverter.GetBytes(NameId("theater_lobby"));
            var bytes = (byte[])native.Bytes.Clone(); var hits = new List<int>();
            for (var at = bytes.AsSpan().IndexOf(from); at >= 0;) { if (bytes.AsSpan(at + 4, 4).SequenceEqual(lobby)) hits.Add(at); var next = bytes.AsSpan(at + 1).IndexOf(from); at = next < 0 ? -1 : at + 1 + next; }
            if (hits.Count != 1) throw new InvalidOperationException("menu graph transition goto_theater -> theater_lobby not unique");
            BitConverter.GetBytes(NameId("goto_campaign_solo")).CopyTo(bytes, hits[0]);
            result = bytes; break;
        }
        default: result = WithSchemaOf(candidate.Bytes, native); break;   // background lua/wpf: Guardians tag as is
    }
    if (hasSource && role is "CampaignLua" or "CampaignWpf" or "BackgroundLua" or "BackgroundWpf") result = Rebind(result, item);
    built[role] = result;
    Console.WriteLine($"{role,-14} {item.GetProperty("Native").GetProperty("Name").GetString()}: {native.Bytes.Length} -> {result.Length} bytes");
}

// ---- override layer module -----------------------------------------------------------------------
var template = FileMetadata.Read(Path.Combine(forge, "deploy", "any", "levels", "globals-rtx-12719-1.module"), "Module", default);
var baseModule = FileMetadata.Read(Path.Combine(forge, "deploy", "any", "levels", "globals-rtx-1.module"), "Module", default);
using var strings = new MemoryStream(); var entries = new List<ModuleWriteEntry>();
foreach (var (role, payload) in built)
{
    var native = tags[role].GetProperty("Native");
    if (native.GetProperty("File").GetString() != "deploy/any/levels/globals-rtx-1.module") throw new InvalidOperationException(role + " is not in the base globals module");
    var entry = baseModule.Entry(native.GetProperty("Item").GetInt32());
    if (entry.ResourceCount != 0 || entry.Parent != -1) throw new InvalidOperationException(role + " has resources");
    var raw = (byte[])entry.Raw.Clone();
    BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(0), (int)strings.Length); BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(12), 0);
    strings.Write(Encoding.UTF8.GetBytes(entry.Name + '\0'));
    entries.Add(new(raw, () => new ModuleWritePayload(payload, RehashAsset: true)));
}
{
    var (raw0, name, payload) = DisplayEntry(forge);
    var raw = (byte[])raw0.Clone(); BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(0), (int)strings.Length); BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(12), 0);
    strings.Write(Encoding.UTF8.GetBytes(name + '\0')); entries.Add(new(raw, () => new ModuleWritePayload(payload, RehashAsset: true)));
}
var layer = Path.Combine(output, "deploy", "any", "levels", "globals-rtx-12720-1.module");
if (File.Exists(layer)) File.Delete(layer);   // never write through a hard link into the vanilla copy
var written = ModuleWriter.Write(layer, new ModuleWriteLayout(template.Header, entries.ToArray(), strings.ToArray(), [], entries.Count));
Console.WriteLine($"wrote {layer}: {written.Entries} tags, {written.Bytes} bytes");

// ---- mapinfo module lists ------------------------------------------------------------------------
var anyLayer = "any\\levels\\globals-rtx-12720-1.module"; var pcLayer = "%(Platform)\\levels\\globals-rtx-12720-1.module";
var artLayer = "%(Platform)\\levels\\ui\\mainmenu\\mainmenu-rtx-12720-1.module";
foreach (var path in Directory.GetFiles(Path.Combine(output, "__cms__", "rtx", "levels"), "*.mapinfo", SearchOption.AllDirectories))
{
    var bytes = File.ReadAllBytes(path); var list = MapInfo.Read(bytes);
    var add = new List<string>();
    if (!list.Items.Contains(anyLayer)) add.Add(anyLayer);
    if (!list.Items.Contains(pcLayer)) add.Add(pcLayer);
    var isMainMenu = path.EndsWith("mainmenu.mapinfo", StringComparison.OrdinalIgnoreCase);
    if (isMainMenu && !list.Items.Contains(artLayer)) add.Add(artLayer);
    // Campaign missions: keep the menu layers loaded too. halo5onpc pinned ~1,500 menu assets in memory so the pause
    // menu still had them after a mission load; without them the pause menu froze. Listing the main menu's own
    // layers in each mission's module list makes those assets part of the mission.
    var isCampaign = path.Contains("\\levels\\campaignworld", StringComparison.OrdinalIgnoreCase) || path.Contains("\\levels\\cinematics", StringComparison.OrdinalIgnoreCase);
    // (Off by default: it broke scenario loading -- "failed to load scenario", "unable to join fireteam". Kept for study.)
    if (isCampaign && Environment.GetEnvironmentVariable("H5BAKE_MENU_IN_MISSIONS") is not null)
        foreach (var menuLayer in new[] { "1", "11587-1", "12719-1" }.SelectMany(n => new[] { $"any\\levels\\ui\\mainmenu\\mainmenu-rtx-{n}.module", $"%(Platform)\\levels\\ui\\mainmenu\\mainmenu-rtx-{n}.module" }).Append(artLayer))
            if (!list.Items.Contains(menuLayer)) add.Add(menuLayer);
    if (add.Count == 0) continue;
    // insert right after the last globals layer so map/menu layers keep their relative order
    var items = list.Items.ToList(); var last = items.FindLastIndex(x => x.Contains("\\levels\\globals-rtx-"));
    if (isMainMenu) { items.InsertRange(last + 1, add.Where(x => x != artLayer)); if (add.Contains(artLayer)) items.Add(artLayer); }
    else { items.InsertRange(last + 1, add.Where(x => !x.Contains("\\ui\\mainmenu\\"))); items.AddRange(add.Where(x => x.Contains("\\ui\\mainmenu\\"))); }
    var updated = MapInfo.Write(bytes, list, items);
    if (File.Exists(path)) File.Delete(path);   // the runner hard-linked these; replace, don't write through
    File.WriteAllBytes(path, updated);
    Console.WriteLine($"{Path.GetRelativePath(output, path)}: +{string.Join(", +", add)}");
}

// ---- campaign UI residency list for the shim (FORGE_UI_RETAIN) -------------------------------------
var residency = Directory.GetFiles(Path.Combine(cache, "inputs", "ui-residency"), "*.bin").OrderByDescending(File.GetLastWriteTimeUtc).FirstOrDefault();
if (residency is null) throw new InvalidOperationException("halo5onpc's UI residency list is missing (inputs\\ui-residency)");
var retain = Path.Combine(output, "h5campaign", "ui-retain.bin"); Directory.CreateDirectory(Path.GetDirectoryName(retain)!);
if (File.Exists(retain)) File.Delete(retain);
File.Copy(residency, retain); Console.WriteLine($"ui residency: {Path.GetFileName(residency)} -> {retain}");

// ---- campaign audio ------------------------------------------------------------------------------
var campaignAudio = Directory.GetFiles(Path.Combine(cache, "game", "audio"), "campaign.pck", SearchOption.AllDirectories).OrderByDescending(File.GetLastWriteTimeUtc).FirstOrDefault();
if (campaignAudio is null) throw new InvalidOperationException("campaign audio package missing (game\\audio)");
string? voiceLanguage = null;
using (var cs = File.OpenRead(campaignAudio))
{
    var head = new byte[28]; cs.ReadExactly(head); var lm = (int)BinaryPrimitives.ReadUInt32LittleEndian(head.AsSpan(12)); var table = new byte[lm]; cs.ReadExactly(table);
    foreach (var dir in Directory.GetDirectories(Path.Combine(forge, "sound", "win")))
    {
        var voice = Path.Combine(dir, "soundvoice.pck"); if (!File.Exists(voice)) continue;
        using var vs = File.OpenRead(voice); var vh = new byte[28]; vs.ReadExactly(vh);
        if (BinaryPrimitives.ReadUInt32LittleEndian(vh.AsSpan(12)) != lm) continue; var vt = new byte[lm]; vs.ReadExactly(vt);
        if (vt.AsSpan().SequenceEqual(table)) { voiceLanguage = Path.GetFileName(dir); break; }
    }
}
if (voiceLanguage is null) throw new InvalidOperationException("no Forge soundvoice.pck shares the campaign audio language table");
var extraBanks = BuildExtraBanks(guardiansSound, Path.Combine(forge, "sound", "win"), campaignAudio, Path.Combine(cache, "inputs", "audio-extra", "extra-recipes.json"), output);
MergeAudio(Path.Combine(forge, "sound", "win", voiceLanguage, "soundvoice.pck"), extraBanks is null ? new[] { campaignAudio } : new[] { campaignAudio, extraBanks }, Path.Combine(output, "sound", "win", voiceLanguage, "soundvoice.pck"));
return 0;

// ---- campaign audio: one package Forge already loads ------------------------------------------------
// Forge loads a fixed set of Wwise packages from sound\win (soundbank, soundstream, soundvoice, ...); halo5onpc
// registered sound\win\h5solo-campaign.pck in memory after startup. Its prepared package holds only banks/media
// that Forge's soundbank/soundstream/soundvoice lack, and it was written with soundvoice's language table, so
// here it (plus the extra banks) is merged into <language>\soundvoice.pck: one AKPK with all lookup tables
// (sorted by id, language) and all payloads, 4 KiB-block aligned. Written next to the target and moved over it,
// so the hard link to the vanilla Forge file is replaced, never written through. First package wins on duplicates.
static void MergeAudio(string nativePck, string[] extraPcks, string target)
{
    static uint U(byte[] b, int o) => BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(o));
    static byte[] Tables(FileStream s) { var h = new byte[28]; s.Position = 0; s.ReadExactly(h); if (!h.AsSpan(0, 4).SequenceEqual("AKPK"u8) || U(h, 8) != 1) throw new InvalidDataException("not an AKPK v1 package");
        var t = new byte[checked((int)U(h, 4) + 8)]; s.Position = 0; s.ReadExactly(t); if (t.Length != 28 + U(t, 12) + U(t, 16) + U(t, 20) + U(t, 24)) throw new InvalidDataException("AKPK table sizes disagree"); return t; }
    static List<(uint Id, uint Block, uint Size, uint Start, uint Lang, int Src)> Lut(byte[] t, int at, int src)
    { var n = U(t, at); var list = new List<(uint, uint, uint, uint, uint, int)>(); for (var i = 0; i < n; i++) { var o = at + 4 + i * 20; list.Add((U(t, o), U(t, o + 4), U(t, o + 8), U(t, o + 12), U(t, o + 16), src)); } return list; }
    var inputs = new[] { nativePck }.Concat(extraPcks).ToArray();
    var stamp = target + ".h5merge";
    var expected = string.Join(" ", inputs.Select(p => { var i = new FileInfo(p); return $"{i.Length}:{i.LastWriteTimeUtc.Ticks}"; }));
    if (File.Exists(stamp) && File.Exists(target) && File.ReadAllText(stamp).Split('\n')[0] == expected && new FileInfo(target).Length.ToString() == File.ReadAllText(stamp).Split('\n').ElementAtOrDefault(1))
    { Console.WriteLine($"audio: {target} already merged"); return; }
    var streams = inputs.Select(File.OpenRead).ToArray();
    try
    {
        var tables = streams.Select(Tables).ToArray();
        var lang = tables[0].AsSpan(28, (int)U(tables[0], 12)).ToArray();
        var banks = new List<(uint Id, uint Block, uint Size, uint Start, uint Lang, int Src)>(); var media = new List<(uint Id, uint Block, uint Size, uint Start, uint Lang, int Src)>();
        var seenB = new HashSet<(uint, uint)>(); var seenM = new HashSet<(uint, uint)>(); int dup = 0;
        for (var i = 0; i < tables.Length; i++)
        {
            var t = tables[i]; int lm = (int)U(t, 12);
            if (!t.AsSpan(28, lm).SequenceEqual(lang)) throw new InvalidDataException(inputs[i] + " has another language table");
            if (U(t, 24) != 4) throw new InvalidDataException("external-source tables are not supported");
            foreach (var e in Lut(t, 28 + lm, i)) if (seenB.Add((e.Id, e.Lang))) banks.Add(e); else dup++;
            foreach (var e in Lut(t, 28 + lm + (int)U(t, 16), i)) if (seenM.Add((e.Id, e.Lang))) media.Add(e); else dup++;
        }
        banks.Sort((x, y) => x.Id != y.Id ? x.Id.CompareTo(y.Id) : x.Lang.CompareTo(y.Lang)); media.Sort((x, y) => x.Id != y.Id ? x.Id.CompareTo(y.Id) : x.Lang.CompareTo(y.Lang));
        var headerLength = 28 + lang.Length + 4 + 20 * banks.Count + 4 + 20 * media.Count + 4;
        var temp = target + ".h5tmp"; if (File.Exists(temp)) File.Delete(temp);
        var placed = new Dictionary<(int Src, uint Id, uint Lang, bool Bank), uint>();
        using (var o = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1 << 20))
        {
            long pos = (headerLength + 4095) / 4096 * 4096; var buffer = new byte[1 << 20];
            foreach (var (list, isBank) in new[] { (banks, true), (media, false) })
                foreach (var e in list.OrderBy(x => x.Src).ThenBy(x => (long)x.Start * x.Block))
                {
                    if (e.Block != 4096) throw new InvalidDataException("unexpected block size " + e.Block);
                    pos = (pos + 4095) / 4096 * 4096; placed[(e.Src, e.Id, e.Lang, isBank)] = checked((uint)(pos / 4096));
                    var src = streams[e.Src]; src.Position = (long)e.Start * e.Block; o.Position = pos;
                    for (long left = e.Size; left > 0;) { var n = (int)Math.Min(buffer.Length, left); src.ReadExactly(buffer, 0, n); o.Write(buffer, 0, n); left -= n; }
                    pos += e.Size;
                }
            o.SetLength(Math.Max(o.Length, pos));
            var h = new byte[headerLength]; "AKPK"u8.CopyTo(h);
            void W(int at, uint v) => BinaryPrimitives.WriteUInt32LittleEndian(h.AsSpan(at), v);
            W(4, (uint)headerLength - 8); W(8, 1); W(12, (uint)lang.Length); W(16, (uint)(4 + 20 * banks.Count)); W(20, (uint)(4 + 20 * media.Count)); W(24, 4);
            lang.CopyTo(h, 28); var c = 28 + lang.Length;
            foreach (var (list, isBank) in new[] { (banks, true), (media, false) })
            { W(c, (uint)list.Count); c += 4; foreach (var e in list) { W(c, e.Id); W(c + 4, e.Block); W(c + 8, e.Size); W(c + 12, placed[(e.Src, e.Id, e.Lang, isBank)]); W(c + 16, e.Lang); c += 20; } }
            W(c, 0); o.Position = 0; o.Write(h);
        }
        foreach (var s in streams) s.Dispose();
        File.Move(temp, target, overwrite: true);
        File.WriteAllText(stamp, expected + "\n" + new FileInfo(target).Length);
        Console.WriteLine($"audio: merged {extraPcks.Length} package(s) into {target}: {banks.Count} banks, {media.Count} media ({dup} duplicates kept from the earlier package), {new FileInfo(target).Length} bytes");
    }
    finally { foreach (var s in streams) s.Dispose(); }
}


// ---- extra campaign banks ----------------------------------------------------------------------------
// halo5onpc's campaign package holds the banks its converted tags reference. Forge's own (native) tags in a mission
// also request Guardians banks Forge never shipped (e.g. sb_001_vo_ai_global: Covenant AI dialogue, Grunt/Elite
// chatter), so those played silent. Every Guardians bank that no Forge package and no campaign package carries is
// converted 118 -> 112 with structural recipes from tools\audio118\conv.py (the same field rules as halo5onpc's
// recipes, re-derived and checked byte-exact against all 704 of them): property/RTPC ids, media locations, sizes.
// Each recipe is applied with a source check and an output checksum, resident WEM media get halo5onpc's channel-mask
// fix, and the banks are written as one more AKPK (soundvoice's language table: sfx 0, english(us) 1) for MergeAudio.
static string? BuildExtraBanks(string vanillaSound, string forgeSound, string campaignPck, string recipesPath, string output)
{
    static uint U(byte[] b, int o) => BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(o));
    static (byte[] Lang, List<(uint Id, uint Block, uint Size, uint Start, uint Lang)> Banks, HashSet<uint> Media) Read(string path)
    {
        using var s = File.OpenRead(path); var h = new byte[28]; s.ReadExactly(h);
        var t = new byte[checked((int)U(h, 4) + 8)]; s.Position = 0; s.ReadExactly(t);
        int lm = (int)U(t, 12), bl = (int)U(t, 16); var banks = new List<(uint, uint, uint, uint, uint)>(); var media = new HashSet<uint>();
        int at = 28 + lm; var n = U(t, at); for (var i = 0; i < n; i++) { var o = at + 4 + i * 20; banks.Add((U(t, o), U(t, o + 4), U(t, o + 8), U(t, o + 12), U(t, o + 16))); }
        at = 28 + lm + bl; n = U(t, at); for (var i = 0; i < n; i++) media.Add(U(t, at + 4 + i * 20));
        return (t.AsSpan(28, lm).ToArray(), banks, media);
    }
    var have = new HashSet<uint>();
    foreach (var dir in new[] { "SFX", "English(US)" })
        foreach (var p in Directory.GetFiles(Path.Combine(forgeSound, dir), "*.pck")) foreach (var e in Read(p).Banks) have.Add(e.Id);
    var campaign = Read(campaignPck); foreach (var e in campaign.Banks) have.Add(e.Id);
    var recipes = new Dictionary<string, JsonElement>();
    using var recipeDoc = JsonDocument.Parse(File.ReadAllBytes(recipesPath));
    if (recipeDoc.RootElement.GetProperty("SourceVersion").GetInt32() != 118 || recipeDoc.RootElement.GetProperty("NativeVersion").GetInt32() != 112) throw new InvalidDataException("extra bank recipes have another version");
    foreach (var r in recipeDoc.RootElement.GetProperty("Recipes").EnumerateArray()) recipes[r.GetProperty("InputSha256").GetString()!] = r;
    byte[] Apply(byte[] raw, uint id)
    {
        var sha = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(raw));
        if (!recipes.TryGetValue(sha, out var r) || r.GetProperty("Id").GetUInt32() != id) throw new InvalidDataException("no structural recipe for this bank");
        using var o = new MemoryStream(r.GetProperty("OutputBytes").GetInt32()); var cursor = 0;
        foreach (var e in r.GetProperty("Edits").EnumerateArray())
        {
            var at = e.GetProperty("Offset").GetInt32(); var before = Convert.FromHexString(e.GetProperty("Before").GetString()!); var after = Convert.FromHexString(e.GetProperty("After").GetString()!);
            if (at < cursor || at > raw.Length - before.Length || !raw.AsSpan(at, before.Length).SequenceEqual(before)) throw new InvalidDataException($"recipe field at {at} failed its source check");
            o.Write(raw, cursor, at - cursor); o.Write(after); cursor = at + before.Length;
        }
        o.Write(raw, cursor, raw.Length - cursor); var bytes = o.ToArray();
        if (Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)) != r.GetProperty("OutputSha256").GetString()) throw new InvalidDataException("converted bank failed its checksum");
        // resident media: same WEM channel-mask conversion halo5onpc applies (non-RIFF plugin data is left as is)
        int didx = -1, didxLen = 0, data = -1;
        for (var at = 0; at < bytes.Length;) { var name = System.Text.Encoding.ASCII.GetString(bytes, at, 4); var size = (int)U(bytes, at + 4); if (name == "DIDX") { didx = at + 8; didxLen = size; } if (name == "DATA") data = at + 8; at += 8 + size; }
        if (didx >= 0 && data >= 0)
            for (var at = didx; at < didx + didxLen; at += 12)
            {
                var off = (int)U(bytes, at + 4); var len = (int)U(bytes, at + 8); var payload = bytes.AsSpan(data + off, len);
                if (len >= 12 && payload[..4].SequenceEqual("RIFF"u8)) AudioConverter.Wem(payload.ToArray()).Bytes.CopyTo(payload);
            }
        return bytes;
    }
    var written = new List<(uint Id, uint Lang, byte[] Bytes)>(); var skipped = new List<uint>();
    foreach (var (pkg, lang) in new[] { (Path.Combine(vanillaSound, "SFX", "soundbank.pck"), 0u), (Path.Combine(vanillaSound, "SFX", "soundstream.pck"), 0u), (Path.Combine(vanillaSound, "English(US)", "soundvoice.pck"), 1u) })
    {
        var src = Read(pkg); using var s = File.OpenRead(pkg);
        foreach (var e in src.Banks.Where(b => !have.Contains(b.Id)))
        {
            var raw = new byte[e.Size]; s.Position = (long)e.Start * e.Block; s.ReadExactly(raw);
            try { written.Add((e.Id, lang, Apply(raw, e.Id))); have.Add(e.Id); }
            catch (Exception ex) { skipped.Add(e.Id); Console.WriteLine($"extra bank {e.Id}: not converted ({ex.Message})"); }
        }
    }
    if (written.Count == 0) { Console.WriteLine("extra banks: none needed"); return null; }
    written.Sort((x, y) => x.Id != y.Id ? x.Id.CompareTo(y.Id) : x.Lang.CompareTo(y.Lang));
    var langTable = campaign.Lang; var headerLength = 28 + langTable.Length + 4 + 20 * written.Count + 4 + 4;
    var path = Path.Combine(output, "h5campaign", "extra-banks.pck"); if (File.Exists(path)) File.Delete(path);
    using (var o = new FileStream(path, FileMode.CreateNew, FileAccess.Write))
    {
        var h = new byte[headerLength]; "AKPK"u8.CopyTo(h); void W(int at, uint v) => BinaryPrimitives.WriteUInt32LittleEndian(h.AsSpan(at), v);
        W(4, (uint)headerLength - 8); W(8, 1); W(12, (uint)langTable.Length); W(16, (uint)(4 + 20 * written.Count)); W(20, 4); W(24, 4); langTable.CopyTo(h, 28);
        long pos = (headerLength + 4095) / 4096 * 4096; var c = 28 + langTable.Length; W(c, (uint)written.Count); c += 4;
        foreach (var (id, l, bytes) in written) { W(c, id); W(c + 4, 4096); W(c + 8, (uint)bytes.Length); W(c + 12, checked((uint)(pos / 4096))); W(c + 16, l); c += 20; o.Position = pos; o.Write(bytes); pos = (pos + bytes.Length + 4095) / 4096 * 4096; }
        W(c, 0); c += 4; W(c, 0); o.Position = 0; o.Write(h);
    }
    Console.WriteLine($"extra banks: {written.Count} converted into {path}, {skipped.Count} without a conversion rule");
    return path;
}

static class MapInfo
{
    public sealed record List(int Field, int Start, int End, string[] Items);
    public static List Read(byte[] d)
    {
        var i = d.AsSpan().IndexOf(new byte[] { 0xCB, 0x07, 0x09 });
        if (i < 0 || d.AsSpan(i + 1).IndexOf(new byte[] { 0xCB, 0x07, 0x09 }) >= 0) throw new InvalidOperationException("module list field not found or not unique");
        var j = i + 3; var count = Varint(d, ref j); var start = j; var items = new string[count];
        for (var k = 0; k < count; k++) { var n = Varint(d, ref j); items[k] = Encoding.ASCII.GetString(d, j, n); j += n; }
        return new(i, start, j, items);
    }
    public static byte[] Write(byte[] d, List list, List<string> items)
    {
        using var o = new MemoryStream(); o.Write(d, 0, list.Field + 3); Put(o, items.Count);
        foreach (var s in items) { var b = Encoding.ASCII.GetBytes(s); Put(o, b.Length); o.Write(b); }
        o.Write(d, list.End, d.Length - list.End); return o.ToArray();
    }
    static int Varint(byte[] d, ref int i) { int v = 0, s = 0; while (true) { var b = d[i++]; v |= (b & 0x7f) << s; s += 7; if ((b & 0x80) == 0) return v; } }
    static void Put(Stream o, int v) { while (true) { var b = (byte)(v & 0x7f); v >>= 7; if (v != 0) o.WriteByte((byte)(b | 0x80)); else { o.WriteByte(b); return; } } }
}
