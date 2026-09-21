using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using Godot;

/// <summary>
/// 模组加载器（Autoload 单例）。
///
/// 职责：
///   1. 启动时自动发现并加载模组（内置 + 外部 dll）。
///   2. 按 [ModSide] 过滤，确保服务端/客户端只加载该加载的模组。
///   3. 在游戏关键节点抛出事件钩子（见下面的 event），模组订阅即可介入逻辑。
///   4. 提供 ServerBroadcast 等辅助 API，方便模组（尤其是服务端模组）反作用于游戏。
///
/// NetworkManager 在玩家加入/离开/聊天等时机调用这里的 Raise* 方法；
/// 模组通过订阅对应 event 拿到通知。这样游戏主体与模组完全解耦。
/// </summary>
public partial class ModLoader : Node
{
    public static ModLoader Instance { get; private set; }

    // ---- 给模组订阅的事件钩子 ----
    public event Action ServerStarted;
    public event Action ClientStarted;
    public event Action<long, string> PlayerJoined;
    public event Action<long> PlayerLeft;
    public event Action<long, string, string> ChatReceived; // (id, name, text)
    public event Action<double> Update;

    /// <summary>网络管理器引用，供 ServerBroadcast 等服务端 API 使用。</summary>
    private NetworkManager _network;

    private readonly List<IMod> _loaded = new();

    public override void _Ready()
    {
        Instance = this;
        _network = GetNode<NetworkManager>("/root/NetworkManager");
        LoadMods();
    }

    public override void _Process(double delta)
    {
        Update?.Invoke(delta);
    }

    // ---------------- 加载逻辑 ----------------

    private void LoadMods()
    {
        // 1) 内置模组：扫描入口程序集里实现 IMod 的类型（含 SampleMod）。
        var builtin = Assembly.GetExecutingAssembly()
            .GetTypes()
            .Where(t => typeof(IMod).IsAssignableFrom(t) && !t.IsInterface && !t.IsAbstract);

        foreach (var type in builtin)
        {
            TryInstantiate(type);
        }

        // 2) 外部模组：扫描 user://mods/*.dll（运行时 Assembly.LoadFrom）。
        //    这是“真·插件/模组”入口——把编译好的 dll 丢进去即可，不用重新打包游戏。
        LoadExternalDlls("user://mods");

        GD.Print($"[ModLoader] 已加载 {_loaded.Count} 个模组。");
    }

    private void LoadExternalDlls(string dir)
    {
        if (!DirAccess.DirExistsAbsolute(ProjectSettings.GlobalizePath(dir)))
        {
            return;
        }
        using var d = DirAccess.Open(dir);
        if (d == null) return;
        d.ListDirBegin();
        string fileName;
        while ((fileName = d.GetNext()) != "")
        {
            if (fileName.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
            {
                string realPath = ProjectSettings.GlobalizePath(dir + "/" + fileName);
                try
                {
                    var asm = Assembly.LoadFrom(realPath);
                    foreach (var type in asm.GetTypes()
                        .Where(t => typeof(IMod).IsAssignableFrom(t) && !t.IsInterface && !t.IsAbstract))
                    {
                        TryInstantiate(type);
                    }
                }
                catch (Exception e)
                {
                    GD.PrintErr($"[ModLoader] 加载模组 {fileName} 失败：{e.Message}");
                }
            }
        }
        d.ListDirEnd();
    }

    private void TryInstantiate(Type type)
    {
        // [ModSide] 仅作元数据记录，不在加载期按运行时角色过滤——因为 Autoload 阶段
        // 服务器/客户端的角色尚未确定（主机/客户端在 _Ready 时 peer 还没建立）。
        // 改为“全部加载”，由模组自己在事件回调里用 Multiplayer.IsServer() 守卫服务端逻辑
        // （见 SampleMod.cs），与“我的世界”插件“代码两边都跑、逻辑按需生效”的语义一致。
        var side = ModSide.Both;
        var attr = type.GetCustomAttribute<ModSideAttribute>();
        if (attr != null) side = attr.Side;

        try
        {
            if (Activator.CreateInstance(type) is IMod mod)
            {
                mod.Init(this);
                _loaded.Add(mod);
                GD.Print($"[ModLoader] 已加载模组：{mod.Id}（{type.Name}，声明作用域 {side}）");
            }
        }
        catch (Exception e)
        {
            GD.PrintErr($"[ModLoader] 实例化模组 {type.Name} 失败：{e.Message}");
        }
    }

    // ---------------- 给 NetworkManager 调用的触发方法 ----------------

    public void RaiseServerStarted() => ServerStarted?.Invoke();
    public void RaiseClientStarted() => ClientStarted?.Invoke();
    public void RaisePlayerJoined(long id, string name) => PlayerJoined?.Invoke(id, name);
    public void RaisePlayerLeft(long id) => PlayerLeft?.Invoke(id);
    public void RaiseChatReceived(long id, string name, string text) => ChatReceived?.Invoke(id, name, text);

    /// <summary>
    /// 服务端模组用：以“系统消息”形式向所有人广播一条聊天（带系统配色）。
    /// 客户端调用无效（无权威）。用于欢迎语、管理通知等。
    /// </summary>
    public void ServerBroadcast(string text)
    {
        if (_network != null && Multiplayer.IsServer())
        {
            _network.SystemChat(text);
        }
    }
}
