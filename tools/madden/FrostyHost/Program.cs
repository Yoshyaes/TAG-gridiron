// FrostyHost. Headless, read only driver for the Frosty 1.0.6.3 SDK (Madden NFL 19 profile).
// Private reference tooling for TAG Gridiron. It never writes to the game install and never
// touches mod or launch code. The Frosty cache lives in the Frosty folder's Caches directory.
//
// Usage
//   FrostyHost.exe inventory <outDir>
//   FrostyHost.exe run <jobs.json> <results.jsonl>
//
// Environment
//   FROSTY_DIR  Frosty Editor folder (default %USERPROFILE%\Downloads\FrostyEditor\FrostyEditor)
//   MADDEN_DIR  Madden NFL 19 install (default D:\Games\Madden NFL 19)

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Serialization;
using System.Text;

[assembly: AssemblyVersion("1.0.6.3")]
[assembly: AssemblyTitle("FrostyHost")]

namespace FrostyHost
{
    public static class Program
    {
        public static string FrostyDir;
        public static string GameDir;

        [STAThread]
        public static int Main(string[] args)
        {
            FrostyDir = Environment.GetEnvironmentVariable("FROSTY_DIR")
                ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), @"Downloads\FrostyEditor\FrostyEditor");
            GameDir = Environment.GetEnvironmentVariable("MADDEN_DIR") ?? @"D:\Games\Madden NFL 19";
            // Resolve output paths before changing directory.
            for (int i = 1; i < args.Length; i++)
                args[i] = Path.GetFullPath(args[i]);
            AppDomain.CurrentDomain.AssemblyResolve += Resolve;
            Environment.CurrentDirectory = FrostyDir;
            try
            {
                return Host.Run(args);
            }
            catch (Exception e)
            {
                Console.Error.WriteLine("FATAL " + e);
                return 1;
            }
        }

        private static Assembly Resolve(object sender, ResolveEventArgs args)
        {
            string name = args.Name.Contains(",") ? args.Name.Substring(0, args.Name.IndexOf(',')) : args.Name;
            string[] candidates;
            if (name == "EbxClasses")
                candidates = new[] { Path.Combine(FrostyDir, "Profiles", Host.SdkFilename() + ".dll") };
            else
                candidates = new[]
                {
                    Path.Combine(FrostyDir, name + ".dll"),
                    Path.Combine(FrostyDir, name + ".exe"),
                    Path.Combine(FrostyDir, "ThirdParty", name + ".dll"),
                    Path.Combine(FrostyDir, "Plugins", name + ".dll"),
                };
            foreach (string c in candidates)
                if (File.Exists(c))
                    return Assembly.LoadFile(c);
            return null;
        }
    }

    public class Logger : FrostySdk.Interfaces.ILogger
    {
        private static string Fmt(string text, object[] vars)
        {
            try { return (vars != null && vars.Length > 0) ? string.Format(text, vars) : text; }
            catch { return text; }
        }
        public void Log(string text, params object[] vars) { Console.WriteLine("[log] " + Fmt(text, vars)); }
        public void LogWarning(string text, params object[] vars) { Console.WriteLine("[warn] " + Fmt(text, vars)); }
        public void LogError(string text, params object[] vars) { Console.WriteLine("[error] " + Fmt(text, vars)); }
    }

    public static class Host
    {
        static FrostySdk.Managers.AssetManager am;

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static string SdkFilename() { return FrostySdk.ProfilesLibrary.SDKFilename; }

        [MethodImpl(MethodImplOptions.NoInlining)]
        public static int Run(string[] args)
        {
            if (args.Length < 2)
            {
                Console.Error.WriteLine("usage: FrostyHost inventory <outDir> | run <jobs.json> <results.jsonl>");
                return 2;
            }
            Init();
            if (args[0] == "inventory")
                return Inventory.Write(am, args[1]);
            if (args[0] == "run" && args.Length >= 3)
                return Jobs.Run(am, args[1], args[2]);
            Console.Error.WriteLine("unknown command " + args[0]);
            return 2;
        }

        static void Init()
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var logger = new Logger();
            // Read only load of Frosty's own config (the mesh exporter's D3D device manager reads it).
            // Config.Save is never called by this host.
            string cfg = Environment.GetEnvironmentVariable("FROSTY_CONFIG")
                ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Frosty\editor_config.json");
            Frosty.Core.Config.Load(cfg);
            // Plugin manager loads plugin assemblies and their registered definitions (the mesh exporter
            // needs the compute shaders MeshSetPlugin registers). No startup actions are executed.
            Frosty.Core.App.PluginManager = new Frosty.Core.PluginManager(logger, Frosty.Core.PluginManagerType.Editor);
            FrostySdk.ProfilesLibrary.Initialize(new List<FrostySdk.Profile>());
            if (!FrostySdk.ProfilesLibrary.Initialize("Madden19"))
                throw new Exception("Madden19 profile not found");
            Directory.CreateDirectory("Caches");

            var fs = new FrostySdk.FileSystem(Program.GameDir);
            foreach (FrostySdk.FileSystemSource source in FrostySdk.ProfilesLibrary.Sources)
                fs.AddSource(source.Path, source.SubDirs);
            fs.Initialize(null);

            var rm = new FrostySdk.Managers.ResourceManager(fs);
            rm.SetLogger(logger);
            rm.Initialize();

            am = new FrostySdk.Managers.AssetManager(fs, rm);
            FrostySdk.TypeLibrary.Initialize(true);
            am.SetLogger(logger);
            am.Initialize(true, new FrostySdk.Managers.AssetManagerImportResult());

            Frosty.Core.App.FileSystem = fs;
            Frosty.Core.App.ResourceManager = rm;
            Frosty.Core.App.AssetManager = am;
            Frosty.Core.App.Logger = logger;

            // The D3D11 device (used by the mesh exporter's tangent space compute shader) fails on its
            // very first lazy creation outside the editor and succeeds on the next call. Warm it up here.
            for (int attempt = 0; attempt < 3; attempt++)
            {
                try { if (Frosty.Core.Viewport.FrostyDeviceManager.Current.GetDevice() != null) break; }
                catch (Exception e) { Console.WriteLine("[init] device warmup attempt " + attempt + " " + e.GetType().Name); }
            }

            uint sdkVer = FrostySdk.TypeLibrary.GetSdkVersion();
            Console.WriteLine("[init] profile=" + FrostySdk.ProfilesLibrary.ProfileName + " head=" + fs.Head + " sdk=" + sdkVer
                + (sdkVer == fs.Head ? " (match)" : " (MISMATCH)") + " ebx=" + am.GetEbxCount() + " in " + sw.Elapsed.TotalSeconds.ToString("0.0") + "s");
        }
    }

    public static class Csv
    {
        public static string Q(object o)
        {
            string s = o == null ? "" : o.ToString();
            if (s.IndexOfAny(new[] { ',', '"', '\n', '\r' }) >= 0)
                s = "\"" + s.Replace("\"", "\"\"") + "\"";
            return s;
        }
        public static string Row(params object[] cols) { return string.Join(",", cols.Select(Q)); }
    }

    public static class Inventory
    {
        public static int Write(FrostySdk.Managers.AssetManager am, string outDir)
        {
            Directory.CreateDirectory(outDir);
            var bundleNames = new Dictionary<int, string>();
            var bundleSb = new Dictionary<int, string>();
            Func<int, string> bname = id =>
            {
                string n;
                if (!bundleNames.TryGetValue(id, out n))
                {
                    var b = am.GetBundleEntry(id);
                    n = b == null ? ("#" + id) : b.Name;
                    bundleNames[id] = n;
                    var sb = b == null ? null : am.GetSuperBundle(b.SuperBundleId);
                    bundleSb[id] = sb == null ? "" : sb.Name;
                }
                return n;
            };

            int count = 0;
            using (var w = new StreamWriter(Path.Combine(outDir, "inventory.csv"), false, new UTF8Encoding(false)))
            {
                w.WriteLine(Csv.Row("kind", "type", "path", "size", "original_size", "bundles", "superbundles", "guid"));
                foreach (var e in am.EnumerateEbx("", false, false, true, ""))
                {
                    var bl = e.Bundles.Select(bname).ToList();
                    var sbl = e.Bundles.Select(id => { bname(id); return bundleSb[id]; }).Distinct().ToList();
                    w.WriteLine(Csv.Row("ebx", e.Type, e.Name, e.Size, e.OriginalSize, string.Join(";", bl), string.Join(";", sbl), e.Guid));
                    count++;
                }
                foreach (var r in am.EnumerateRes(0, false, ""))
                {
                    var bl = r.Bundles.Select(bname).ToList();
                    var sbl = r.Bundles.Select(id => { bname(id); return bundleSb[id]; }).Distinct().ToList();
                    w.WriteLine(Csv.Row("res", r.Type, r.Name, r.Size, r.OriginalSize, string.Join(";", bl), string.Join(";", sbl), r.ResRid.ToString("X16")));
                    count++;
                }
                foreach (var c in am.EnumerateChunks(false))
                {
                    w.WriteLine(Csv.Row("chunk", "chunk", c.Id, c.Size, c.LogicalSize, c.Bundles.Count, "", c.H32));
                    count++;
                }
            }
            using (var w = new StreamWriter(Path.Combine(outDir, "bundles.csv"), false, new UTF8Encoding(false)))
            {
                w.WriteLine(Csv.Row("bundle_id", "bundle", "superbundle"));
                foreach (var kv in bundleNames.OrderBy(k => k.Key))
                    w.WriteLine(Csv.Row(kv.Key, kv.Value, bundleSb[kv.Key]));
            }
            Console.WriteLine("[inventory] rows=" + count + " bundles=" + bundleNames.Count);
            return 0;
        }
    }

    public static class Jobs
    {
        public static int Run(FrostySdk.Managers.AssetManager am, string jobsPath, string resultsPath)
        {
            var jobs = Newtonsoft.Json.Linq.JArray.Parse(File.ReadAllText(jobsPath));
            int ok = 0, fail = 0, skip = 0;
            using (var w = new StreamWriter(resultsPath, true, new UTF8Encoding(false)))
            {
                foreach (Newtonsoft.Json.Linq.JObject job in jobs)
                {
                    var res = new Newtonsoft.Json.Linq.JObject();
                    res["id"] = job["id"];
                    res["op"] = job["op"];
                    res["asset"] = job["asset"];
                    var sw = System.Diagnostics.Stopwatch.StartNew();
                    try
                    {
                        string status = Exec(am, job, res);
                        res["status"] = status;
                        if (status == "skipped") skip++; else ok++;
                    }
                    catch (Exception e)
                    {
                        Exception inner = e;
                        while (inner is TargetInvocationException && inner.InnerException != null) inner = inner.InnerException;
                        res["status"] = "fail";
                        res["error"] = inner.GetType().Name + ": " + inner.Message;
                        res["trace"] = inner.StackTrace == null ? "" : inner.StackTrace.Split('\n').Take(6).Aggregate("", (a, b) => a + b.Trim() + " | ");
                        fail++;
                    }
                    res["seconds"] = Math.Round(sw.Elapsed.TotalSeconds, 2);
                    w.WriteLine(res.ToString(Newtonsoft.Json.Formatting.None));
                    w.Flush();
                    Console.WriteLine("[job] " + res["status"] + " " + job["op"] + " " + job["asset"] + (res["error"] != null ? " :: " + res["error"] : ""));
                }
            }
            Console.WriteLine("[jobs] ok=" + ok + " skipped=" + skip + " fail=" + fail);
            return 0;
        }

        static string S(Newtonsoft.Json.Linq.JObject j, string k, string d = "") { var t = j[k]; return t == null ? d : (string)t; }
        static bool B(Newtonsoft.Json.Linq.JObject j, string k, bool d) { var t = j[k]; return t == null ? d : (bool)t; }

        static FrostySdk.Managers.EbxAssetEntry Entry(FrostySdk.Managers.AssetManager am, string name)
        {
            var e = am.GetEbxEntry(name);
            if (e == null) throw new Exception("ebx not found: " + name);
            return e;
        }

        static string Exec(FrostySdk.Managers.AssetManager am, Newtonsoft.Json.Linq.JObject job, Newtonsoft.Json.Linq.JObject res)
        {
            string op = S(job, "op");
            string asset = S(job, "asset");
            string outPath = S(job, "out");
            bool skipExisting = B(job, "skip_existing", true);
            if (outPath != "") Directory.CreateDirectory(Path.GetDirectoryName(outPath));

            switch (op)
            {
                case "mesh":
                {
                    if (skipExisting && File.Exists(outPath)) return "skipped";
                    var entry = Entry(am, asset);
                    dynamic meshAsset = am.GetEbx(entry, false).RootObject;
                    ulong resRid = (ulong)meshAsset.MeshSetResource;
                    var rEntry = am.GetResEntry(resRid);
                    var meshSet = am.GetResAs<MeshSetPlugin.Resources.MeshSet>(rEntry, null);
                    string skeleton = S(job, "skeleton");
                    var task = (Frosty.Core.Windows.FrostyTaskWindow)FormatterServices.GetUninitializedObject(typeof(Frosty.Core.Windows.FrostyTaskWindow));
                    var exporter = new MeshSetPlugin.FBXExporter(task);
                    string tmp = outPath + ".partial.fbx";
                    if (File.Exists(tmp)) File.Delete(tmp);
                    exporter.ExportFBX(meshAsset, tmp, S(job, "version", "2012"), S(job, "units", "Meters"),
                        B(job, "flatten", false), B(job, "single_lod", true), skeleton, "binary", new[] { meshSet });
                    if (!File.Exists(tmp)) throw new Exception("exporter produced no file");
                    if (File.Exists(outPath)) File.Delete(outPath);
                    File.Move(tmp, outPath);
                    res["outputs"] = new Newtonsoft.Json.Linq.JArray(outPath);
                    res["mesh_type"] = meshSet.Type.ToString();
                    return "ok";
                }
                case "texture":
                {
                    var formats = job["formats"] == null ? new List<string> { "dds", "png" } : job["formats"].Select(t => (string)t).ToList();
                    var outs = formats.Select(f => outPath + "." + f).ToList();
                    if (skipExisting && outs.All(File.Exists)) return "skipped";
                    var entry = Entry(am, asset);
                    dynamic texAsset = am.GetEbx(entry, false).RootObject;
                    ulong resRid = (ulong)texAsset.Resource;
                    var tex = am.GetResAs<FrostySdk.Resources.Texture>(am.GetResEntry(resRid), null);
                    var exporter = new TexturePlugin.TextureExporter();
                    var arr = new Newtonsoft.Json.Linq.JArray();
                    for (int i = 0; i < formats.Count; i++)
                    {
                        if (skipExisting && File.Exists(outs[i])) { arr.Add(outs[i]); continue; }
                        string tmp = outs[i] + ".partial." + formats[i];
                        exporter.Export(tex, tmp, "*." + formats[i]);
                        if (File.Exists(outs[i])) File.Delete(outs[i]);
                        File.Move(tmp, outs[i]);
                        arr.Add(outs[i]);
                    }
                    res["outputs"] = arr;
                    res["pixel_format"] = tex.PixelFormat;
                    res["width"] = tex.Width;
                    res["height"] = tex.Height;
                    res["mips"] = tex.MipCount;
                    res["tex_type"] = tex.Type.ToString();
                    res["slices"] = tex.SliceCount;
                    return "ok";
                }
                case "ebx":
                {
                    if (skipExisting && File.Exists(outPath)) return "skipped";
                    var entry = Entry(am, asset);
                    var ebx = am.GetEbx(entry, false);
                    var json = EbxDump.Dump(am, entry, ebx, job["max_list"] == null ? 20000 : (int)job["max_list"]);
                    File.WriteAllText(outPath, json.ToString(Newtonsoft.Json.Formatting.Indented), new UTF8Encoding(false));
                    res["outputs"] = new Newtonsoft.Json.Linq.JArray(outPath);
                    return "ok";
                }
                case "meshinfo":
                {
                    if (skipExisting && File.Exists(outPath)) return "skipped";
                    var entry = Entry(am, asset);
                    dynamic meshAsset = am.GetEbx(entry, false).RootObject;
                    var meshSet = am.GetResAs<MeshSetPlugin.Resources.MeshSet>(am.GetResEntry((ulong)meshAsset.MeshSetResource), null);
                    var o = new Newtonsoft.Json.Linq.JObject();
                    o["asset"] = asset;
                    o["type"] = meshSet.Type.ToString();
                    o["name"] = meshSet.Name;
                    var lods = new Newtonsoft.Json.Linq.JArray();
                    foreach (var lod in meshSet.Lods)
                    {
                        var lo = new Newtonsoft.Json.Linq.JObject();
                        lo["name"] = lod.ShortName;
                        lo["bone_count"] = lod.BoneCount;
                        lo["bone_index_array"] = new Newtonsoft.Json.Linq.JArray(lod.BoneIndexArray.Cast<object>().ToArray());
                        lo["bone_short_names"] = new Newtonsoft.Json.Linq.JArray(lod.BoneShortNameArray.Cast<object>().ToArray());
                        var secs = new Newtonsoft.Json.Linq.JArray();
                        foreach (var s in lod.Sections)
                        {
                            var so = new Newtonsoft.Json.Linq.JObject();
                            so["name"] = s.Name;
                            so["material_id"] = s.MaterialId;
                            so["vertex_count"] = s.VertexCount;
                            so["primitive_count"] = s.PrimitiveCount;
                            so["bones_per_vertex"] = s.BonesPerVertex;
                            so["bone_list_len"] = s.BoneList.Count;
                            so["bone_list_max"] = s.BoneList.Count == 0 ? -1 : (int)s.BoneList.Cast<object>().Select(Convert.ToInt32).Max();
                            so["tangent_compression"] = s.TangentSpaceCompressionType.ToString();
                            secs.Add(so);
                        }
                        lo["sections"] = secs;
                        lods.Add(lo);
                    }
                    o["lods"] = lods;
                    File.WriteAllText(outPath, o.ToString(Newtonsoft.Json.Formatting.Indented), new UTF8Encoding(false));
                    res["outputs"] = new Newtonsoft.Json.Linq.JArray(outPath);
                    return "ok";
                }
                case "deps":
                {
                    var entry = Entry(am, asset);
                    var arr = new Newtonsoft.Json.Linq.JArray();
                    foreach (Guid g in entry.EnumerateDependencies())
                    {
                        var d = am.GetEbxEntry(g);
                        arr.Add(d == null ? g.ToString() : (d.Name + "|" + d.Type));
                    }
                    res["deps"] = arr;
                    return "ok";
                }
                case "raw":
                {
                    // Raw bytes of a res entry or a chunk, for format research (animation banks).
                    if (skipExisting && File.Exists(outPath)) return "skipped";
                    Stream s;
                    string kind = S(job, "kind", "res");
                    if (kind == "res")
                        s = am.GetRes(am.GetResEntry(asset));
                    else if (kind == "resrid")
                        s = am.GetRes(am.GetResEntry(Convert.ToUInt64(asset, 16)));
                    else
                        s = am.GetChunk(am.GetChunkEntry(new Guid(asset)));
                    if (s == null) throw new Exception("no data");
                    using (s)
                    using (var f = File.Create(outPath))
                        s.CopyTo(f);
                    res["outputs"] = new Newtonsoft.Json.Linq.JArray(outPath);
                    return "ok";
                }
                default:
                    throw new Exception("unknown op " + op);
            }
        }
    }

    public static class EbxDump
    {
        public static Newtonsoft.Json.Linq.JObject Dump(FrostySdk.Managers.AssetManager am, FrostySdk.Managers.EbxAssetEntry entry, FrostySdk.IO.EbxAsset ebx, int maxList)
        {
            var objects = ebx.Objects.ToList();
            var index = new Dictionary<object, int>(new RefEq());
            for (int i = 0; i < objects.Count; i++) index[objects[i]] = i;
            var root = new Newtonsoft.Json.Linq.JObject();
            root["asset"] = entry.Name;
            root["type"] = entry.Type;
            root["file_guid"] = ebx.FileGuid.ToString();
            var deps = new Newtonsoft.Json.Linq.JArray();
            foreach (Guid g in ebx.Dependencies)
            {
                var d = am.GetEbxEntry(g);
                deps.Add(d == null ? g.ToString() : d.Name);
            }
            root["dependencies"] = deps;
            var objs = new Newtonsoft.Json.Linq.JArray();
            var ctx = new Ctx { am = am, index = index, maxList = maxList };
            for (int i = 0; i < objects.Count; i++)
            {
                var o = Obj(ctx, objects[i], 0);
                o["__index"] = i;
                objs.Add(o);
            }
            root["objects"] = objs;
            return root;
        }

        class Ctx
        {
            public FrostySdk.Managers.AssetManager am;
            public Dictionary<object, int> index;
            public int maxList;
            public Dictionary<Guid, string> extNames = new Dictionary<Guid, string>();
        }

        class RefEq : IEqualityComparer<object>
        {
            public new bool Equals(object a, object b) { return ReferenceEquals(a, b); }
            public int GetHashCode(object o) { return RuntimeHelpers.GetHashCode(o); }
        }

        static Newtonsoft.Json.Linq.JObject Obj(Ctx ctx, object o, int depth)
        {
            var j = new Newtonsoft.Json.Linq.JObject();
            Type t = o.GetType();
            j["__type"] = t.Name;
            foreach (var p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (p.GetIndexParameters().Length > 0) continue;
                if (p.GetCustomAttributes(typeof(FrostySdk.Attributes.EbxFieldMetaAttribute), true).Length == 0) continue;
                object v;
                try { v = p.GetValue(o); } catch (Exception e) { j[p.Name] = "<error " + e.GetType().Name + ">"; continue; }
                j[p.Name] = Val(ctx, v, depth + 1);
            }
            return j;
        }

        static Newtonsoft.Json.Linq.JToken Val(Ctx ctx, object v, int depth)
        {
            if (v == null) return Newtonsoft.Json.Linq.JValue.CreateNull();
            if (depth > 24) return "<depth>";
            Type t = v.GetType();
            if (v is string || t.IsPrimitive || v is decimal) return new Newtonsoft.Json.Linq.JValue(v);
            if (t.IsEnum) return v.ToString();
            if (v is Guid) return v.ToString();
            if (v is FrostySdk.Ebx.ResourceRef) return ((ulong)(FrostySdk.Ebx.ResourceRef)v).ToString("X16");
            if (v is FrostySdk.Ebx.CString || v is FrostySdk.Ebx.FileRef || v is FrostySdk.Ebx.TypeRef || v is FrostySdk.Sha1) return v.ToString();
            if (v is FrostySdk.Ebx.PointerRef)
            {
                var pr = (FrostySdk.Ebx.PointerRef)v;
                if (pr.Type == FrostySdk.IO.PointerRefType.Null) return Newtonsoft.Json.Linq.JValue.CreateNull();
                var r = new Newtonsoft.Json.Linq.JObject();
                if (pr.Type == FrostySdk.IO.PointerRefType.Internal)
                {
                    int idx;
                    r["ref"] = ctx.index.TryGetValue(pr.Internal, out idx) ? idx : -1;
                    r["ref_type"] = pr.Internal.GetType().Name;
                }
                else
                {
                    Guid fg = pr.External.FileGuid;
                    string name;
                    if (!ctx.extNames.TryGetValue(fg, out name))
                    {
                        var e = ctx.am.GetEbxEntry(fg);
                        name = e == null ? fg.ToString() : e.Name;
                        ctx.extNames[fg] = name;
                    }
                    r["ext"] = name;
                    r["class_guid"] = pr.External.ClassGuid.ToString();
                }
                return r;
            }
            if (v is FrostySdk.Ebx.BoxedValueRef) return Val(ctx, ((FrostySdk.Ebx.BoxedValueRef)v).Value, depth + 1);
            if (v is IList)
            {
                var list = (IList)v;
                var a = new Newtonsoft.Json.Linq.JArray();
                int n = Math.Min(list.Count, ctx.maxList);
                for (int i = 0; i < n; i++) a.Add(Val(ctx, list[i], depth + 1));
                if (list.Count > n) a.Add("<truncated " + list.Count + ">");
                return a;
            }
            // Structs and classes from the EbxClasses SDK, or math types.
            if (t.GetProperties().Any(p => p.GetCustomAttributes(typeof(FrostySdk.Attributes.EbxFieldMetaAttribute), true).Length > 0))
                return Obj(ctx, v, depth);
            return v.ToString();
        }
    }
}
