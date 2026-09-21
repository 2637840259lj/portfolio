using Godot;

/// <summary>
/// 玩家：服务端权威（server-authoritative）模型。
///
/// 同步策略：
///   - 权威端（服务器）每物理帧模拟后，把真实位置写入 <see cref="ServerPosition"/>。
///   - MultiplayerSynchronizer 只同步 ServerPosition（见 player.tscn），不再直接同步 position。
///   - 客户端（含“自己的节点”，因为本机玩家也是服务器模拟的）每帧把渲染用的 position
///     向 ServerPosition 插值，消除网络抖动带来的卡顿。
///
/// 节点名 = 玩家 ID；权威方默认是服务器(peer 1)，所以“服务器模拟、客户端只渲染”自然成立。
/// </summary>
public partial class Player : CharacterBody2D
{
    [Export] public float Speed { get; set; } = 300.0f;

    /// <summary>被 MultiplayerSynchronizer 同步的“权威位置”。</summary>
    [Export] public Vector2 ServerPosition { get; set; } = Vector2.Zero;

    // 服务器侧缓存的“该玩家最新输入方向”，由客户端通过 SubmitInput RPC 写入。
    private Vector2 _inputDir = Vector2.Zero;

    private Label _nameLabel;
    private string _nickname = "";
    private Color _color = Colors.White;

    public string Nickname => _nickname;
    public Color PlayerColor => _color;

    /// <summary>生成时由 Game.cs 调用，写入昵称与颜色并设置标签。</summary>
    public void Setup(string nickname, Color color)
    {
        _nickname = nickname;
        _color = color;
        if (_nameLabel != null)
        {
            _nameLabel.Text = nickname;
            _nameLabel.Modulate = color;
        }
    }

    public override void _Ready()
    {
        _nameLabel = GetNodeOrNull<Label>("NameLabel");
        if (_nameLabel != null)
        {
            _nameLabel.Text = _nickname;
            _nameLabel.Modulate = _color;
        }
    }

    public override void _PhysicsProcess(double delta)
    {
        if (Multiplayer.MultiplayerPeer == null) return;

        long myId = Multiplayer.GetUniqueId();
        bool isMyOwnNode = Name == myId.ToString();

        if (IsMultiplayerAuthority())
        {
            // 权威端模拟一切：
            //   - 本机玩家（仅主机模式）：直接读键盘；
            //   - 远端客户端玩家：使用对方通过 RPC 上报的 _inputDir。
            Vector2 dir = isMyOwnNode
                ? Input.GetVector("ui_left", "ui_right", "ui_up", "ui_down")
                : _inputDir;

            Velocity = dir * Speed;
            MoveAndSlide();
            ServerPosition = Position; // 镜像真实位置供同步
        }
        else
        {
            // 客户端：只处理“我自己的玩家”节点，上报输入；其余节点仅渲染。
            if (isMyOwnNode)
            {
                Vector2 dir = Input.GetVector("ui_left", "ui_right", "ui_up", "ui_down");
                RpcId(1, nameof(SubmitInput), dir);
            }
            // 插值：渲染位置平滑趋向权威位置（系数越大越跟手，越小越平滑）。
            float t = Mathf.Clamp((float)(delta * 12.0), 0f, 1f);
            Position = Position.Lerp(ServerPosition, t);
        }
    }

    /// <summary>客户端 → 服务器：上报本玩家的输入方向（Unreliable，丢包不影响）。</summary>
    [Rpc(MultiplayerApi.RpcMode.AnyPeer, TransferMode = MultiplayerPeer.TransferModeEnum.Unreliable)]
    private void SubmitInput(Vector2 dir)
    {
        if (!IsMultiplayerAuthority()) return;
        _inputDir = dir;
    }
}
