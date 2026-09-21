using Godot;
using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;

/// <summary>
/// 网络管理器（Autoload 单例）。
/// 职责：建立/断开连接、维护玩家档案与名单、广播加入/离开、转发聊天、
///       在服务端做“昵称+房间密码”准入校验（含重名处理）、触发模组钩子。
///
/// 它不负责生成玩家节点——那是 game.tscn 里 Game.cs 的工作（监听本类的信号）。
/// </summary>
public partial class NetworkManager : Node
{
    // ---------- 常量 ----------

    private const int Port = 7777;
    private const string DefaultServerIp = "127.0.0.1";
    private const int MaxConnections = 8;
    private const double ConnectTimeoutSec = 5.0;
    private const int NicknameMaxLen = 16;
    private const int ChatMaxLen = 200;
    private const double ChatMinIntervalSec = 0.5;

    /// <summary>握手协议版本。客户端/服务器不一致直接拒绝，避免“连上了但各种诡异不同步”。</summary>
    private const string ProtocolVersion = "1.0.0";

    /// <summary>系统消息配色（金）。</summary>
    private static readonly Color SystemColor = new(1.0f, 0.8f, 0.3f);

    // 出生点：环形分布，避免玩家重叠生成。数量与默认最大人数一致。
    private static readonly Vector2[] SpawnPoints =
    {
        new(200, 200), new(600, 200), new(200, 500), new(600, 500),
        new(400, 120), new(400, 580), new(120, 350), new(680, 350),
    };

    // 玩家配色调色板（按出生点槽位取，重连/重名都不影响）。
    private static readonly Color[] Palette =
    {
        Colors.Red, Colors.Blue, Colors.Green, Colors.Yellow,
        Colors.Orange, Colors.Purple, Colors.Cyan, Colors.Pink,
    };

    //--------信号--------

    [Signal] public delegate void PlayerConnectedEventHandler(long id);
    [Signal] public delegate void PlayerDisconnectedEventHandler(long id);
    [Signal] public delegate void ServerDisconnectedEventHandler();
    [Signal] public delegate void ConnectionFailedEventHandler();
    /// <summary>加入被拒绝（密码错/重名/满员）：带原因，主菜单据此提示。</summary>
    [Signal] public delegate void JoinRejectedEventHandler(string reason);
    /// <summary>收到一条聊天（含系统消息）。Chat.cs 订阅它来渲染。</summary>
    [Signal] public delegate void ChatMessageEventHandler(string name, Color color, string text);

    //--------状态--------

    /// <summary>已通过认证的玩家 ID 名单。</summary>
    public readonly HashSet<long> Players = new();

    /// <summary>玩家档案（昵称/颜色/出生点），服务端权威，客户端从 SpawnPlayer 同步得到。</summary>
    public readonly Dictionary<long, PlayerInfo> PlayerProfiles = new();

    private ENetMultiplayerPeer _peer;
    private bool _connecting;
    private SceneTreeTimer _connectTimer;

    // 准入相关
    private string _serverPasswordHash = "";           // 服务端：房间密码哈希（空 = 无密码）
    private string _pendingNickname = "";              // 客户端：待发送的昵称
    private string _pendingPasswordHash = "";          // 客户端：待发送的密码哈希

    // 主机模式：本机玩家（服务器自身）的登记延迟到游戏场景加载后再做，
    // 否则在菜单/Autoload 阶段就 Rpc(SpawnPlayer) 会触发同步器在节点尚不存在时提前初始化，
    // 导致客户端收不到“服务器自身玩家节点”的位置同步（表现为房主在别人眼里不动）。
    private bool _hostPending;
    private string _pendingHostNickname = "Host";
    private int _pendingHostSpawnIndex;

    // 出生点空闲槽位（队列）
    private readonly Queue<int> _freeSpawns = new();

    // 聊天限频（按发送者记录上次发言时刻，毫秒）
    private readonly Dictionary<long, ulong> _lastChatMs = new();

    // 聊天历史（最近若干条），新客户端进场景后回放，避免错过系统欢迎语等早到消息。
    private readonly List<(string Name, Color Color, string Text)> _chatHistory = new();
    private const int ChatHistoryMax = 50;

    // 断线重连：记住上一次加入参数，主菜单“重新连接”可一键重连。
    private string _lastAddress = "";
    private int _lastPort;
    private string _lastNickname = "";
    private string _lastPasswordHash = "";
    private bool _hasLastConnection;

    // server.cfg 热更 / 管理员指令
    private HashSet<string> _adminNames = new(StringComparer.OrdinalIgnoreCase);
    private string _lastCfgHash = "";
    private bool _hotReloadActive;
    private ServerConfig _serverConfig;
    private double _reloadCheckAccum;

    //--------玩家档案--------

    /// <summary>单个玩家的展示信息。</summary>
    public class PlayerInfo
    {
        public string Nickname;
        public Color Color;
        public int SpawnIndex;
        public Vector2 Spawn;

        public PlayerInfo(string nickname, Color color, int spawnIndex, Vector2 spawn)
        {
            Nickname = nickname;
            Color = color;
            SpawnIndex = spawnIndex;
            Spawn = spawn;
        }
    }

    //--------生命周期--------

    public override void _Ready()
    {
        ResetSpawnPool();
        Multiplayer.PeerConnected += OnPeerConnected;
        Multiplayer.PeerDisconnected += OnPeerDisconnected;
        Multiplayer.ConnectedToServer += OnConnectedToServer;
        Multiplayer.ConnectionFailed += OnConnectionFailed;
        Multiplayer.ServerDisconnected += OnServerDisconnected;
    }

    private void ResetSpawnPool()
    {
        _freeSpawns.Clear();
        for (int i = 0; i < SpawnPoints.Length; i++) _freeSpawns.Enqueue(i);
    }

    // --------对外接口：主菜单调用--------

    /// <summary>创建房间（主机模式）。主机自己也算一名玩家。</summary>
    public bool CreateGame(int port = 0, string password = "", string nickname = "Host")
    {
        Cleanup();
        ResetSpawnPool();

        // 加载 server.cfg（密码/端口/最大人数/管理员）。优先级：显式参数 > 配置 > 内置默认。
        LoadServerConfig();
        if (port == 0) port = (_serverConfig != null && _serverConfig.Port > 0) ? _serverConfig.Port : Port;
        if (_serverConfig != null && !string.IsNullOrEmpty(_serverConfig.Password) && string.IsNullOrEmpty(password))
            _serverPasswordHash = HashPassword(_serverConfig.Password);
        else
            _serverPasswordHash = HashPassword(password);

        _adminNames.Add(nickname); // 房主恒为管理员

        _peer = new ENetMultiplayerPeer();
        Error error = _peer.CreateServer(port, MaxConnections);
        if (error != Error.Ok)
        {
            GD.PrintErr($"创建服务器失败：{error}");
            _peer = null;
            return false;
        }

        Multiplayer.MultiplayerPeer = _peer;

        long serverId = Multiplayer.GetUniqueId(); // 主机模式下恒为 1
        // 主机占用 0 号出生点（先预留槽位，避免客户端抢占到），
        // 但“登记本机玩家”延迟到游戏场景加载后由 Game._Ready 调 SpawnPendingHost 完成。
        _pendingHostSpawnIndex = _freeSpawns.Count > 0 ? _freeSpawns.Dequeue() : 0;
        _pendingHostNickname = nickname;
        _hostPending = true;
        GD.Print($"房间已创建，端口：{port}，本机玩家ID：{serverId}");

        ModLoader.Instance?.RaiseServerStarted();
        return true;
    }

    /// <summary>
    /// 主机模式专用：游戏场景（game.tscn）加载完成后由 Game._Ready 调用，
    /// 此时节点树已就绪，再登记并广播本机玩家，同步器才能正常把“服务器自身玩家节点”
    /// 的位置同步给客户端。请勿在菜单/Autoload 阶段直接登记主机玩家。
    /// </summary>
    public void SpawnPendingHost()
    {
        if (!_hostPending) return;
        _hostPending = false;
        long serverId = Multiplayer.GetUniqueId();
        AcceptPlayer(serverId, _pendingHostNickname, _pendingHostSpawnIndex);
    }

    /// <summary>启动无头专用服务器。自身不占玩家名额、不生成本地玩家节点。</summary>
    public bool StartDedicatedServer(int port = 0, int maxPlayers = 0, string password = "")
    {
        Cleanup();
        ResetSpawnPool();

        // 加载 server.cfg（仅专用服务器模式读取；优先级：显式参数 > 配置 > 内置默认）。
        LoadServerConfig();
        if (port == 0) port = (_serverConfig != null && _serverConfig.Port > 0) ? _serverConfig.Port : Port;
        int max = maxPlayers > 0 ? maxPlayers : ((_serverConfig != null && _serverConfig.MaxPlayers > 0) ? _serverConfig.MaxPlayers : MaxConnections);
        if (_serverConfig != null && !string.IsNullOrEmpty(_serverConfig.Password) && string.IsNullOrEmpty(password))
            _serverPasswordHash = HashPassword(_serverConfig.Password);
        else
            _serverPasswordHash = HashPassword(password);

        _peer = new ENetMultiplayerPeer();
        Error error = _peer.CreateServer(port, max);
        if (error != Error.Ok)
        {
            GD.PrintErr($"启动专用服务器失败：{error}");
            _peer = null;
            return false;
        }

        Multiplayer.MultiplayerPeer = _peer;
        GD.Print($"[专用服务器] 已在端口 {port} 启动，最大连接 {max}，等待客户端...");
        GD.Print($"SERVER_READY port={port}");

        ModLoader.Instance?.RaiseServerStarted();
        return true;
    }

    // 客户端加入相关：先记住参数并切到游戏场景（含 MultiplayerSpawner），
    // 再由 Game._Ready 在场景就绪后调用 JoinGameFromScene 真正发起连接。
    // 关键：连入瞬间服务器会 replay“房主节点（peer 1）”给客户端，客户端此时必须有 spawner，
    // 否则报 “Cannot find spawn node / spawner is null”。旧流程是“先连后切场景”，导致该报错。
    private string _pendingAddress = DefaultServerIp;
    private int _pendingPort;
    private bool _joinPending;

    /// <summary>主菜单“加入”：暂存参数并切到游戏场景，连接推迟到 Game._Ready（场景含 spawner）之后。</summary>
    public void BeginJoin(string address, int port, string nickname, string password)
    {
        _pendingAddress = string.IsNullOrEmpty(address) ? DefaultServerIp : address;
        _pendingPort = port == 0 ? Port : port;
        _pendingNickname = (nickname ?? "").Trim();
        _pendingPasswordHash = HashPassword(password ?? "");
        _joinPending = true;
        // 用 CallDeferred 避免“在 _Ready 期间切换场景导致 tree busy”的报错（测试 harness 会这样调用）。
        GetTree().CallDeferred("change_scene_to_file", "res://game.tscn");
    }

    /// <summary>游戏场景就绪后（仅客户端）由 Game._Ready 调用，用暂存参数真正发起连接。</summary>
    public void JoinGameFromScene()
    {
        if (!_joinPending) return;
        ConnectInternal();
    }

    /// <summary>用上一次的参数重连（主菜单“重新连接”按钮调用）。同样先回游戏场景再连。</summary>
    public bool Reconnect()
    {
        if (!_hasLastConnection) return false;
        _pendingAddress = _lastAddress;
        _pendingPort = _lastPort;
        _pendingNickname = _lastNickname;
        _pendingPasswordHash = _lastPasswordHash;
        _joinPending = true;
        GetTree().CallDeferred("change_scene_to_file", "res://game.tscn");
        return true;
    }

    public bool HasLastConnection => _hasLastConnection;

    /// <summary>客户端当前是否“已暂存加入参数、待 Game._Ready 发起连接”。Game._Ready 据此判断。</summary>
    public bool HasPendingJoin => _joinPending;

    /// <summary>真正发起连接：创建 ENet 客户端 peer 并交给 Multiplayer。passwordHash 已是哈希值。</summary>
    private bool ConnectInternal()
    {
        string address = _pendingAddress;
        int port = _pendingPort;
        string nickname = _pendingNickname;
        string passwordHash = _pendingPasswordHash;

        Cleanup();
        // Cleanup 不会清空加入暂存，这里重新兜底赋值以防万一。
        _pendingAddress = address;
        _pendingPort = port;
        _pendingNickname = nickname;
        _pendingPasswordHash = passwordHash;

        // 记住本次参数，供重连使用。
        _lastAddress = address;
        _lastPort = port;
        _lastNickname = nickname;
        _lastPasswordHash = passwordHash;
        _hasLastConnection = true;
        _joinPending = false;

        _peer = new ENetMultiplayerPeer();
        Error error = _peer.CreateClient(address, port);
        if (error != Error.Ok)
        {
            GD.PrintErr($"发起连接失败：{error}");
            _peer = null;
            return false;
        }

        Multiplayer.MultiplayerPeer = _peer;

        _connecting = true;
        _connectTimer = GetTree().CreateTimer(ConnectTimeoutSec);
        _connectTimer.Timeout += OnConnectTimeout;

        GD.Print($"正在连接{address}:{port}...");
        return true;
    }

    /// <summary>断开连接，清理玩家名单与出生点。</summary>
    public void Cleanup()
    {
        if (_connectTimer != null)
        {
            _connectTimer.Timeout -= OnConnectTimeout;
            _connectTimer = null;
        }
        _connecting = false;
        Multiplayer.MultiplayerPeer = null;
        _peer = null;
        Players.Clear();
        PlayerProfiles.Clear();
        _freeSpawns.Clear();
        _hostPending = false;
        _joinPending = false;
        _lastChatMs.Clear();
        _adminNames.Clear();
        _hotReloadActive = false;
        _reloadCheckAccum = 0;
    }

    // --------连接生命周期--------

    private void OnPeerConnected(long id)
    {
        if (!Multiplayer.IsServer()) return;
        // 注意：此时只建立了传输层连接，还没过“昵称+密码”准入。
        // 不能在这里生成玩家——必须等 RequestJoin 校验通过。
    }

    private void OnPeerDisconnected(long id)
    {
        if (!Multiplayer.IsServer()) return;

        // 只有已通过认证的玩家才需要完整离开流程；未过准入的就直接忽略。
        if (!Players.Contains(id)) return;

        if (PlayerProfiles.TryGetValue(id, out var info))
        {
            _freeSpawns.Enqueue(info.SpawnIndex); // 归还出生点槽位
        }
        PlayerProfiles.Remove(id);
        if (Players.Remove(id))
        {
            EmitSignal(SignalName.PlayerDisconnected, id);
        }
        ModLoader.Instance?.RaisePlayerLeft(id);
        GD.Print($"[服务器] 玩家 {id} 已断开，剩余玩家：{Players.Count}");
        Rpc(MethodName.DespawnPlayer, id);
    }

    private void OnConnectedToServer()
    {
        _connecting = false;
        GD.Print($"连接成功！本机玩家ID：{Multiplayer.GetUniqueId()}");
        // 连接成功只是传输层通了，立刻发起“昵称+密码+协议版本”准入请求。
        RpcId(1, nameof(RequestJoin), _pendingNickname, _pendingPasswordHash, ProtocolVersion);
    }

    private void OnConnectionFailed()
    {
        if (!_connecting) return;
        Cleanup();
        EmitSignal(SignalName.ConnectionFailed);
        GD.PrintErr("连接失败：请确认服务器已创建房间，检查IP和端口。");
        // 客户端此时已身处游戏场景（先于连接），连不上就回主菜单。
        if (!Multiplayer.IsServer())
            GetTree().ChangeSceneToFile("res://main.tscn");
    }

    private void OnConnectTimeout()
    {
        _connectTimer = null;
        if (!_connecting) return;
        GD.PrintErr("连接超时（5s）：服务器可能未启动或网络不可达。");
        Cleanup();
        EmitSignal(SignalName.ConnectionFailed);
        if (!Multiplayer.IsServer())
            GetTree().ChangeSceneToFile("res://main.tscn");
    }

    private void OnServerDisconnected()
    {
        // 若已经被 JoinRejected 处理过（_peer 已清空），这里直接忽略，避免重复回菜单。
        if (_peer == null) return;
        Cleanup();
        EmitSignal(SignalName.ServerDisconnected);
        GetTree().ChangeSceneToFile("res://main.tscn");
    }

    // ---------- 准入握手（核心）----------

    /// <summary>
    /// 客户端 → 服务器：发送昵称与密码哈希。RpcMode.AnyPeer 允许任意客户端调用，
    /// 方法体只在服务端（权威）执行并做校验。
    /// </summary>
    [Rpc(MultiplayerApi.RpcMode.AnyPeer, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void RequestJoin(string nickname, string passwordHash, string clientVersion)
    {
        if (!Multiplayer.IsServer()) return;
        long caller = Multiplayer.GetRemoteSenderId();

        // 0) 协议版本校验（防止客户端/服务器版本不一致导致“连上了但不动”之类诡异问题）。
        if (clientVersion != ProtocolVersion)
        {
            Reject(caller, $"版本不兼容（服务器 {ProtocolVersion}，客户端 {clientVersion}）");
            return;
        }

        // 1) 房间密码
        if (_serverPasswordHash.Length > 0 && passwordHash != _serverPasswordHash)
        {
            Reject(caller, "房间密码错误");
            return;
        }

        // 2) 昵称合法性（1~16 字，不能为空，不能是被占用的系统名）
        string name = (nickname ?? "").Trim();
        if (name.Length == 0 || name.Length > NicknameMaxLen || name == "[系统]")
        {
            Reject(caller, $"昵称不合法（1-{NicknameMaxLen}字）");
            return;
        }

        // 3) 重名：昵称在房间内必须唯一（离线模式的“重名问题”解法）。
        //    若将来接入 Steam，身份改为 SteamID（天然唯一），显示名允许重复并自动加后缀。
        foreach (var kv in PlayerProfiles)
        {
            if (kv.Value.Nickname.Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                Reject(caller, "昵称已被占用，请换一个");
                return;
            }
        }

        // 4) 出生点（满员则拒绝）
        if (_freeSpawns.Count == 0)
        {
            Reject(caller, "服务器已满");
            return;
        }

        int idx = _freeSpawns.Dequeue();
        AcceptPlayer(caller, name, idx);

        // 5) 把“已有玩家”私下发给新客户端，让它补生成旧节点。
        foreach (var kv in PlayerProfiles)
        {
            if (kv.Key == caller) continue;
            var p = kv.Value;
            RpcId(caller, MethodName.SpawnPlayer, kv.Key, p.Nickname, p.Color, p.Spawn);
        }

        // 6) 通知新客户端“准入通过”，它据此切换到游戏世界。
        var myInfo = PlayerProfiles[caller];
        RpcId(caller, nameof(JoinAccepted), caller, name, myInfo.Color, myInfo.Spawn);

        GD.Print($"[服务器] 客户端 {caller}（{name}）已加入，当前在线：{Players.Count}");
    }

    /// <summary>校验失败：告知原因并主动断开该 peer。</summary>
    private void Reject(long caller, string reason)
    {
        RpcId(caller, nameof(JoinDenied), reason);
        if (Multiplayer.MultiplayerPeer is ENetMultiplayerPeer enet)
        {
            enet.DisconnectPeer((int)caller, false);
        }
        GD.Print($"[服务器] 拒绝 {caller} 加入：{reason}");
    }

    /// <summary>服务端权威：登记一名玩家（主机自己或刚通过准入的客户端）。</summary>
    private void AcceptPlayer(long id, string nickname, int spawnIndex)
    {
        Color color = Palette[spawnIndex % Palette.Length];
        Vector2 spawn = SpawnPoints[spawnIndex % SpawnPoints.Length];
        PlayerProfiles[id] = new PlayerInfo(nickname, color, spawnIndex, spawn);
        if (Players.Add(id))
        {
            EmitSignal(SignalName.PlayerConnected, id);
            ModLoader.Instance?.RaisePlayerJoined(id, nickname);
        }
        // 广播生成（CallLocal=true 让服务器本地也生成自己的节点；客户端据信号生成）。
        Rpc(MethodName.SpawnPlayer, id, nickname, color, spawn);
    }

    /// <summary>服务器 → 客户端：登记一名玩家的档案（昵称/颜色/出生点）并触发节点生成。</summary>
    [Rpc(MultiplayerApi.RpcMode.Authority, CallLocal = true, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void SpawnPlayer(long id, string nickname, Color color, Vector2 spawn)
    {
        if (id <= 0) return;
        if (!PlayerProfiles.ContainsKey(id))
        {
            // 出生点槽位在客户端无从得知，这里用 dict 现有信息即可（仅展示用）。
            PlayerProfiles[id] = new PlayerInfo(nickname, color, -1, spawn);
        }
        if (Players.Add(id))
        {
            EmitSignal(SignalName.PlayerConnected, id);
        }
    }

    /// <summary>服务器 → 客户端：注销一名玩家。</summary>
    [Rpc(MultiplayerApi.RpcMode.Authority, CallLocal = true, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void DespawnPlayer(long id)
    {
        PlayerProfiles.Remove(id);
        if (Players.Remove(id))
        {
            EmitSignal(SignalName.PlayerDisconnected, id);
        }
    }

    /// <summary>服务器 → 被拒客户端：告知原因，回主菜单。</summary>
    [Rpc(MultiplayerApi.RpcMode.Authority, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void JoinDenied(string reason)
    {
        Cleanup();
        EmitSignal(SignalName.JoinRejected, reason);
        GetTree().ChangeSceneToFile("res://main.tscn");
    }

    /// <summary>服务器 → 新客户端：准入通过。客户端在发起连接前已进入游戏场景（含 spawner），
    /// 这里只需登记自身档案（幂等），切勿再切场景，否则会重载场景丢失已生成的玩家节点。</summary>
    [Rpc(MultiplayerApi.RpcMode.Authority, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void JoinAccepted(long id, string nickname, Color color, Vector2 spawn)
    {
        if (Multiplayer.IsServer()) return; // 仅客户端处理
        // 登记自身档案（和随后到达的 SpawnPlayer 幂等）。
        if (!PlayerProfiles.ContainsKey(id))
        {
            PlayerProfiles[id] = new PlayerInfo(nickname, color, -1, spawn);
        }
        Players.Add(id);
        ModLoader.Instance?.RaiseClientStarted();
    }

    // ---------- 聊天 ----------

    /// <summary>客户端/主机调用：把聊天发给服务器（peer 1）中转。这是公开的“发送入口”。</summary>
    public void SendChatMessage(string text)
    {
        if (Multiplayer.MultiplayerPeer == null) return;
        if (Multiplayer.IsServer())
        {
            // 主机模式：本地就是服务器，直接处理，避免 RpcId(1, ...) 把消息发给自己触发
            // “RPC on yourself is not allowed by selected mode” 报错。
            HandleIncomingChat(Multiplayer.GetUniqueId(), text);
        }
        else
        {
            RpcId(1, MethodName.SendChat, text);
        }
    }

    /// <summary>客户端 → 服务器：发送聊天。服务端限频限长后中转给所有人（RPC 处理器，服务端执行）。</summary>
    [Rpc(MultiplayerApi.RpcMode.AnyPeer, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void SendChat(string text)
    {
        if (!Multiplayer.IsServer()) return;
        long sender = Multiplayer.GetRemoteSenderId();
        HandleIncomingChat(sender, text);
    }

    /// <summary>服务端实际的中转逻辑（房主本地调用与 RPC 调用共用，避免重复）。</summary>
    private void HandleIncomingChat(long sender, string text)
    {
        // 限频：同一发送者最短间隔。
        ulong now = Time.GetTicksMsec();
        if (_lastChatMs.TryGetValue(sender, out ulong last) && (now - last) < ChatMinIntervalSec * 1000)
        {
            return;
        }
        _lastChatMs[sender] = now;

        if (string.IsNullOrWhiteSpace(text)) return;
        if (text.Length > ChatMaxLen) text = text.Substring(0, ChatMaxLen);

        string name = PlayerProfiles.TryGetValue(sender, out var p) ? p.Nickname : "?";

        // 管理员指令：以 “/” 开头且发送者属于管理员列表，则走指令处理、不当作普通聊天广播。
        if (_adminNames.Contains(name) && text.StartsWith("/"))
        {
            HandleAdminCommand(sender, text);
            return;
        }

        if (string.IsNullOrWhiteSpace(text)) return;
        if (text.Length > ChatMaxLen) text = text.Substring(0, ChatMaxLen);

        Color color = p != null ? p.Color : Colors.White;
        GD.Print($"[聊天] 收到 {name}: {text}");
        Rpc(MethodName.ReceiveChat, name, color, text);
    }

    /// <summary>服务器/任一端：收到一条聊天（含系统消息）。CallLocal 让发言方也能看到自己发的。
    /// 这里同时写入聊天历史，供新客户端进场景后回放（避免错过早到的欢迎语等）。</summary>
    [Rpc(MultiplayerApi.RpcMode.Authority, CallLocal = true, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void ReceiveChat(string name, Color color, string text)
    {
        if (_chatHistory.Count >= ChatHistoryMax) _chatHistory.RemoveAt(0);
        _chatHistory.Add((name, color, text));
        EmitSignal(SignalName.ChatMessage, name, color, text);
    }

    /// <summary>聊天历史快照，供 Chat UI 进场景后回放（避免错过系统欢迎语等早到消息）。</summary>
    public IReadOnlyList<(string Name, Color Color, string Text)> ChatHistory => _chatHistory;

    /// <summary>服务端用：以“系统消息”形式向所有人广播（模组/服务器通知）。</summary>
    public void SystemChat(string text)
    {
        if (!Multiplayer.IsServer()) return;
        Rpc(MethodName.ReceiveChat, "[系统]", SystemColor, text);
    }

    // ---------- server.cfg 热更 / 管理员指令 ----------

    /// <summary>读取 server.cfg 并刷新管理员名单与热更指纹。专用服务器与主机模式都会调用。</summary>
    private void LoadServerConfig()
    {
        _serverConfig = new ServerConfig();
        _serverConfig.Load();
        _adminNames.Clear();
        if (!string.IsNullOrEmpty(_serverConfig.Admin))
        {
            foreach (var raw in _serverConfig.Admin.Split(','))
            {
                var a = raw.Trim();
                if (a.Length > 0) _adminNames.Add(a);
            }
        }
        _lastCfgHash = ComputeCfgHash();
        _hotReloadActive = true;
    }

    /// <summary>读取配置文件原文作为指纹，内容变化即触发热更。</summary>
    private string ComputeCfgHash()
    {
        string path = Godot.FileAccess.FileExists("user://server.cfg") ? "user://server.cfg"
                    : (Godot.FileAccess.FileExists("res://server.cfg") ? "res://server.cfg" : "");
        if (path.Length == 0) return "";
        using var f = Godot.FileAccess.Open(path, Godot.FileAccess.ModeFlags.Read);
        return f?.GetAsText() ?? "";
    }

    public override void _Process(double delta)
    {
        if (!_hotReloadActive || !Multiplayer.IsServer()) return;
        _reloadCheckAccum += delta;
        if (_reloadCheckAccum < 3.0) return; // 每 3 秒检查一次
        _reloadCheckAccum = 0;

        string cur = ComputeCfgHash();
        if (cur == _lastCfgHash) return;
        _lastCfgHash = cur;

        _serverConfig = new ServerConfig();
        _serverConfig.Load();
        _adminNames.Clear();
        if (!string.IsNullOrEmpty(_serverConfig.Admin))
        {
            foreach (var raw in _serverConfig.Admin.Split(','))
            {
                var a = raw.Trim();
                if (a.Length > 0) _adminNames.Add(a);
            }
        }
        if (!string.IsNullOrEmpty(_serverConfig.Password))
            _serverPasswordHash = HashPassword(_serverConfig.Password);

        SystemChat("[系统] 服务器配置已热更新（密码 / 管理员列表）。");
        GD.Print("[ServerConfig] 配置热更新完成");
    }

    /// <summary>管理员指令分发（仅管理员昵称触发）。</summary>
    private void HandleAdminCommand(long sender, string text)
    {
        var parts = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 0) return;
        string cmd = parts[0].ToLowerInvariant();
        switch (cmd)
        {
            case "/kick":
                if (parts.Length < 2) { AdminNotice(sender, "用法：/kick <昵称>"); return; }
                KickByName(sender, parts[1]);
                break;
            case "/say":
                string msg = text.Length > parts[0].Length ? text.Substring(parts[0].Length).Trim() : "";
                if (string.IsNullOrEmpty(msg)) { AdminNotice(sender, "用法：/say <消息>"); return; }
                SystemChat($"[管理员] {msg}");
                break;
            case "/reload":
                _lastCfgHash = ""; // 强制下一帧重新加载
                AdminNotice(sender, "已触发配置热重载");
                break;
            case "/list":
                var sb = new StringBuilder();
                sb.Append("在线玩家(").Append(Players.Count).Append(")：");
                foreach (var id in Players)
                    sb.Append(PlayerProfiles.TryGetValue(id, out var p) ? p.Nickname + " " : "");
                AdminNotice(sender, sb.ToString());
                break;
            default:
                AdminNotice(sender, $"未知指令：{cmd}（支持 /kick /say /reload /list）");
                break;
        }
    }

    /// <summary>按昵称踢出玩家（管理员指令）。被踢者触发正常断线流程。</summary>
    private void KickByName(long admin, string targetName)
    {
        long targetId = -1;
        foreach (var kv in PlayerProfiles)
        {
            if (kv.Value.Nickname.Equals(targetName, StringComparison.OrdinalIgnoreCase)) { targetId = kv.Key; break; }
        }
        if (targetId <= 0) { AdminNotice(admin, $"未找到玩家：{targetName}"); return; }
        if (targetId == Multiplayer.GetUniqueId()) { AdminNotice(admin, "不能踢出服务器自身"); return; }
        if (Multiplayer.MultiplayerPeer is ENetMultiplayerPeer enet) enet.DisconnectPeer((int)targetId, false);
        SystemChat($"[系统] 玩家 {targetName} 已被管理员移出");
        GD.Print($"[管理员] 已将 {targetName} 移出");
    }

    /// <summary>只发给某个客户端的私系统消息（管理员指令反馈）。房主（服务器自身）直接本地触发，避免 “RPC on yourself” 报错。</summary>
    private void AdminNotice(long target, string text)
    {
        if (target == Multiplayer.GetUniqueId())
            EmitSignal(SignalName.ChatMessage, "[系统]", SystemColor, text);
        else
            RpcId(target, nameof(AdminNoticeClient), text);
    }

    [Rpc(MultiplayerApi.RpcMode.Authority, TransferMode = MultiplayerPeer.TransferModeEnum.Reliable)]
    private void AdminNoticeClient(string text)
    {
        EmitSignal(SignalName.ChatMessage, "[系统]", SystemColor, text);
    }

    // -------- 工具 --------

    /// <summary>SHA256 十六进制哈希，避免密码明文在网络上跑（即便只是局域网，也养成习惯）。</summary>
    public static string HashPassword(string s)
    {
        using var sha = SHA256.Create();
        byte[] bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(s ?? ""));
        var sb = new StringBuilder(bytes.Length * 2);
        foreach (byte b in bytes) sb.Append(b.ToString("x2"));
        return sb.ToString();
    }
}
