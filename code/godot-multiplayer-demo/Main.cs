using Godot;

/// <summary>
/// 主菜单：收集昵称/密码/IP/端口，调用 NetworkManager，并根据结果切换场景或提示。
/// 自己完全不碰网络对象——分层的好处。
/// </summary>
public partial class Main : Control
{
    [Export] public PackedScene GameScene { get; set; }
    [Export] public Button joinButton;
    [Export] public Button hostButton;
    private Button _reconnectButton;

    [Export] private LineEdit _ipInput;
    [Export] private LineEdit _portInput;
    [Export] private LineEdit _nicknameInput;
    [Export] private LineEdit _passwordInput;
    [Export] private Label _statusLabel;

    /// <summary>跨场景重载保留的提示文案（拒绝原因 / 连接失败等）。</summary>
    private static string _pendingStatus = "";

    public override void _Ready()
    {
        joinButton.Pressed += OnJoinPressed;
        hostButton.Pressed += OnHostPressed;

        var net = GetNode<NetworkManager>("/root/NetworkManager");
        net.JoinRejected += OnJoinRejected;
        net.ConnectionFailed += OnConnectionFailed;

        // 断线重连按钮：仅在曾成功加入过（记住了上次连接参数）时才显示。
        _reconnectButton = new Button
        {
            Name = "ReconnectButton",
            Text = "重新连接(上次)",
            LayoutMode = 0,
            OffsetLeft = 332, OffsetTop = 610, OffsetRight = 468, OffsetBottom = 663,
        };
        _reconnectButton.AddThemeFontSizeOverride("font_size", 32);
        _reconnectButton.Visible = net.HasLastConnection;
        AddChild(_reconnectButton);
        _reconnectButton.Pressed += OnReconnectPressed;

        // 显示上一次留下的状态（例如被服务器拒绝的原因）。
        if (!string.IsNullOrEmpty(_pendingStatus) && _statusLabel != null)
        {
            _statusLabel.Text = _pendingStatus;
            _pendingStatus = "";
        }
    }

    public override void _ExitTree()
    {
        var net = GetNodeOrNull<NetworkManager>("/root/NetworkManager");
        if (net != null)
        {
            net.JoinRejected -= OnJoinRejected;
            net.ConnectionFailed -= OnConnectionFailed;
        }
    }

    private void OnHostPressed()
    {
        var net = GetNode<NetworkManager>("/root/NetworkManager");
        int port = int.TryParse(_portInput.Text.Trim(), out int p) ? p : 0;
        string nickname = string.IsNullOrWhiteSpace(_nicknameInput.Text) ? "Host" : _nicknameInput.Text.Trim();
        string password = _passwordInput.Text; // 空 = 无密码

        if (!net.CreateGame(port, password, nickname))
        {
            SetStatus("创建房间失败，换一个端口试试。");
            return;
        }
        SwitchToGame();
    }

    private void OnJoinPressed()
    {
        var net = GetNode<NetworkManager>("/root/NetworkManager");
        int port = int.TryParse(_portInput.Text.Trim(), out int p) ? p : 0;
        string nickname = _nicknameInput.Text.Trim();
        string password = _passwordInput.Text;

        if (string.IsNullOrEmpty(nickname))
        {
            SetStatus("请先填写昵称。");
            return;
        }

        // 不在这里直接连接——先切到游戏场景（含 spawner），连接由 Game._Ready 发起，
        // 确保连入瞬间客户端已具备 spawner，能正确接收服务器 replay 的“房主节点”。
        net.BeginJoin(_ipInput.Text.Trim(), port, nickname, password);
        SetStatus("正在连接服务器...");
    }

    private void OnJoinRejected(string reason)
    {
        _pendingStatus = "加入失败：" + reason;
    }

    private void OnConnectionFailed()
    {
        _pendingStatus = "连接失败：服务器未开或网络不可达。";
    }

    private void OnReconnectPressed()
    {
        var net = GetNode<NetworkManager>("/root/NetworkManager");
        if (!net.HasLastConnection)
        {
            SetStatus("没有可重连的会话，请手动加入。");
            return;
        }
        if (!net.Reconnect())
        {
            SetStatus("重连失败，请手动加入。");
            return;
        }
        SetStatus("正在重连...");
        // Reconnect 内部会先切到游戏场景（含 spawner），连接由 Game._Ready 发起，无需这里手动切场景。
    }

    private void SetStatus(string text)
    {
        if (_statusLabel != null) _statusLabel.Text = text;
    }

    private void SwitchToGame()
    {
        if (GameScene == null)
        {
            GD.PrintErr("GameScene 没有绑定！");
            return;
        }
        GetTree().ChangeSceneToPacked(GameScene);
    }
}
