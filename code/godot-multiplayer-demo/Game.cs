using Godot;
using Godot.Collections;
using System.Collections.Generic;

/// <summary>
/// 游戏场景：负责生成/删除玩家节点，并把昵称、颜色、出生点套到节点上。
///
/// 节点生成统一走 MultiplayerSpawner（而非手动 AddChild）——只有“服务器权威”一端调用
/// spawner.Spawn(...)， spawner 会自动把节点复制到所有客户端，并由 MultiplayerSynchronizer
/// 同步位置。这样“服务器自身玩家（主机，peer 1）的节点”也能在客户端连入后被正确复制与同步，
/// 否则手动 AddChild 会在客户端连入时因缺少 spawner 导致该节点同步握手失败（表现为房主在别人眼里不动）。
/// 不判断“谁该加入”——那是服务器的决定，这里只执行。
/// </summary>
public partial class Game : Node2D
{
    [Export] public PackedScene PlayerScene { get; set; }

    private NetworkManager _net;
    private Node _playersRoot;
    private MultiplayerSpawner _spawner;
    private readonly HashSet<long> _spawned = new();

    public override void _Ready()
    {
        _net = GetNode<NetworkManager>("/root/NetworkManager");
        _playersRoot = GetNode("Players");
        _spawner = GetNode<MultiplayerSpawner>("MultiplayerSpawner");
        _spawner.SpawnFunction = new Callable(this, nameof(OnSpawn));

        // 客户端优先：若本场景是因“加入/重连”而加载（已暂存加入参数），先真正发起连接。
        // 注意：Multiplayer.IsServer() 在“尚无 peer”时会返回 true（单机默认），所以必须先用
        // HasPendingJoin 判断，且服务器分支要加 “peer 非 null” 守卫，否则未连入的客户端会被误判为服务器。
        if (_net.HasPendingJoin)
        {
            // 客户端：本场景（含 spawner）已就绪，用暂存参数真正发起连接。
            // 这样连入瞬间服务器 replay“房主节点”时，客户端 spawner 已存在，避免 “spawner is null”。
            _net.JoinGameFromScene();
        }
        else if (Multiplayer.MultiplayerPeer != null && Multiplayer.IsServer())
        {
            // 主机 / 专用服务器：登记本机玩家（服务器自身），此时游戏场景已加载，
            // 同步器可在节点存在时初始化，把“服务器自身玩家节点”正确同步给后续连入的客户端。
            _net.SpawnPendingHost();
            foreach (long id in _net.Players) SpawnPlayerNode(id);
        }

        _net.PlayerConnected += OnPlayerConnected;
        _net.PlayerDisconnected += OnPlayerDisconnected;
        _net.ServerDisconnected += OnNetworkEnded;
        _net.ConnectionFailed += OnNetworkEnded;
    }

    public override void _ExitTree()
    {
        _net.PlayerConnected -= OnPlayerConnected;
        _net.PlayerDisconnected -= OnPlayerDisconnected;
        _net.ServerDisconnected -= OnNetworkEnded;
        _net.ConnectionFailed -= OnNetworkEnded;
    }

    // 仅服务器调用：收到“有玩家加入”信号后，通过 spawner 生成其节点（自动复制到客户端）。
    private void OnPlayerConnected(long id)
    {
        if (Multiplayer.IsServer()) SpawnPlayerNode(id);
    }

    // 由 MultiplayerSpawner 在所有端调用：用携带的档案数据实例化并设置玩家节点。
    // 返回未入树的节点，由 spawner 自动加入 spawn_path（Players）。
    private Node OnSpawn(Variant data)
    {
        var d = (Godot.Collections.Dictionary)data;
        long id = (long)d["id"];
        string nick = (string)d["nick"];
        Color color = (Color)d["color"];
        Vector2 spawn = (Vector2)d["spawn"];

        var player = PlayerScene.Instantiate<Player>();
        player.Name = id.ToString();      // 保持 “节点名 = 玩家ID” 约定（RPC/同步器路径一致）
        player.Position = spawn;
        player.ServerPosition = spawn;    // 客户端首帧插值目标正确，不会从 (0,0) 飞过来
        player.Setup(nick, color);
        return player;
    }

    private void OnPlayerDisconnected(long id)
    {
        // 服务器释放节点后，spawner 会自动在客户端回收该节点；这里仅做兜底清理与去重记录。
        var player = _playersRoot.GetNodeOrNull(id.ToString());
        if (player != null) player.QueueFree();
        _spawned.Remove(id);
    }

    private void OnNetworkEnded() => GetTree().ChangeSceneToFile("res://main.tscn");

    // 服务器侧生成：把玩家档案打包进 spawn 数据，交由 spawner 复制到各端（含昵称/颜色/出生点）。
    private void SpawnPlayerNode(long id)
    {
        if (_spawned.Contains(id)) return;
        if (!_net.PlayerProfiles.TryGetValue(id, out var info))
        {
            GD.PrintErr($"[Game] 未找到玩家 {id} 的档案，跳过生成");
            return;
        }
        _spawned.Add(id);

        var data = new Godot.Collections.Dictionary
        {
            ["id"] = id,
            ["nick"] = info.Nickname,
            ["color"] = info.Color,
            ["spawn"] = info.Spawn,
        };
        _spawner.Spawn(data);
    }
}
