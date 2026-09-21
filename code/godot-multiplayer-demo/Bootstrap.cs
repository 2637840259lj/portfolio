using Godot;

/// <summary>
/// 启动引导（Autoload，最先运行）。
///
/// 命令行：
///   --server            无头专用服务器模式
///   --port=7777 / --port 7777    监听端口（覆盖 server.cfg）
///   --max-players=16 / --max-players 16   最大人数（覆盖 server.cfg）
///
/// 其他配置（房间密码等）从 server.cfg 读取（见 ServerConfig.cs）。
/// 普通双击/编辑器运行不带 --server，走正常客户端主菜单流程。
///
/// 运行示例（无头）：
///   Godot.exe --headless --path "E:\godot\multiplayer-demo" --server --port 7777 --max-players 16
/// </summary>
public partial class Bootstrap : Node
{
    private bool _testClientActive;
    private bool _testHostActive;
    private double _moveLogAccum;

    public override void _Ready()
    {
        string[] args = OS.GetCmdlineArgs();
        bool dedicated = false;
        bool testClient = false;
        bool testHost = false;
        int port = 0;
        int maxPlayers = 0;

        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            if (a == "--server" || a == "-server") dedicated = true;
            else if (a == "--testclient" || a == "-testclient") testClient = true;
            else if (a == "--testhost" || a == "-testhost") testHost = true;
            else if (a.StartsWith("--port=")) int.TryParse(a.Substring("--port=".Length), out port);
            else if (a == "--port" && i + 1 < args.Length) int.TryParse(args[++i], out port);
            else if (a.StartsWith("--max-players=")) int.TryParse(a.Substring("--max-players=".Length), out maxPlayers);
            else if (a == "--max-players" && i + 1 < args.Length) int.TryParse(args[++i], out maxPlayers);
        }

        // 读取 server.cfg（密码 / 默认端口 / 默认人数）。命令行参数优先级更高。
        var cfg = new ServerConfig();
        cfg.Load();
        if (port == 0) port = cfg.Port;
        if (maxPlayers == 0) maxPlayers = cfg.MaxPlayers;
        string password = cfg.Password;

        if (dedicated)
        {
            var net = GetNode<NetworkManager>("/root/NetworkManager");
            if (!net.StartDedicatedServer(port, maxPlayers, password))
            {
                GD.PrintErr("[专用服务器] 启动失败。");
                return;
            }
            GetTree().CallDeferred("change_scene_to_file", "res://game.tscn");
            GD.Print("[专用服务器] 已请求载入游戏世界（延迟切换）...");
            return;
        }

        if (testClient)
        {
            var net = GetNode<NetworkManager>("/root/NetworkManager");
            // 测试客户端同样走“先切游戏场景、由 Game._Ready 发起连接”的流程，与正式客户端一致。
            net.BeginJoin("127.0.0.1", port == 0 ? 7777 : port, "Bot1", "");
            _testClientActive = true;
            // 连入后 3 秒发一条聊天，验证“聊天回环”；12 秒后退出（含场景切换与连接握手余量）。
            var chatTimer = GetTree().CreateTimer(3.0);
            chatTimer.Timeout += () => net.SendChatMessage("大家好，我是 Bot1（自动化测试）");
            var quitTimer = GetTree().CreateTimer(12.0);
            quitTimer.Timeout += () => GetTree().Quit();
            GD.Print("[测试客户端] 已请求进入游戏世界并在场景就绪后连接，3s 后发消息，12s 后退出...");
            return;
        }

        if (testHost)
        {
            var net = GetNode<NetworkManager>("/root/NetworkManager");
            net.CreateGame(port == 0 ? 7777 : port, password, "Host");
            GetTree().CallDeferred("change_scene_to_file", "res://game.tscn");
            _testHostActive = true;
            var quitTimer = GetTree().CreateTimer(20.0);
            quitTimer.Timeout += () => GetTree().Quit();
            GD.Print("[测试主机] 已创建房间并进入游戏世界，模拟右移，7s 后退出...");
            return;
        }
        // 普通客户端，交给 Main 菜单。
    }

    public override void _Process(double delta)
    {
        if (!_testClientActive && !_testHostActive) return;
        var net = GetNodeOrNull<NetworkManager>("/root/NetworkManager");
        if (net == null || !net.IsInsideTree() || net.Multiplayer.MultiplayerPeer == null) return;

        var scene = GetTree().CurrentScene;

        // 测试客户端：把“一直按住右方向”上报给服务器，并观察同步回来的 ServerPosition。
        if (_testClientActive)
        {
            long myId = net.Multiplayer.GetUniqueId();
            var me = scene?.GetNodeOrNull<Player>($"Players/{myId}");
            if (me != null && net.Multiplayer.MultiplayerPeer != null &&
                net.Multiplayer.MultiplayerPeer.GetConnectionStatus() == MultiplayerPeer.ConnectionStatus.Connected)
            {
                me.RpcId(1, "SubmitInput", Vector2.Right);
            }
        }

        // 测试主机：模拟“一直按住右方向键”，由本机权威节点读取键盘移动。
        if (_testHostActive)
        {
            Input.ActionPress("ui_right");
        }

        _moveLogAccum += delta;
        if (_moveLogAccum < 1.0) return;
        _moveLogAccum = 0;

        var me2 = scene?.GetNodeOrNull<Player>(_testClientActive ? $"Players/{net.Multiplayer.GetUniqueId()}" : "Players/1");
        if (me2 != null)
            GD.Print($"[测试] 自己 ServerPos={me2.ServerPosition} Pos={me2.Position} 权威={me2.IsMultiplayerAuthority()}");

        var host = scene?.GetNodeOrNull<Player>("Players/1");
        if (host != null && host.Name != me2?.Name)
            GD.Print($"[测试] 房主 ServerPos={host.ServerPosition} Pos={host.Position} 权威={host.IsMultiplayerAuthority()}");
    }
}
