using Godot;

/// <summary>
/// 聊天 UI 逻辑（挂在 game.tscn 的 Chat 节点上）。
/// - 监听 NetworkManager.ChatMessage，把每条消息按昵称颜色渲染到 RichTextLabel。
/// - 回车打开输入框；在输入框里回车发送；Esc 取消输入。
/// - 发送走 NetworkManager.SendChat（服务端限频限长后中转，见 NetworkManager）。
/// </summary>
public partial class Chat : Control
{
    private RichTextLabel _log;
    private LineEdit _input;
    private NetworkManager _net;

    public override void _Ready()
    {
        _log = GetNode<RichTextLabel>("ChatLog");
        _input = GetNode<LineEdit>("ChatInput");
        _net = GetNode<NetworkManager>("/root/NetworkManager");

        _log.Set("bbcode_enabled", true);
        _input.TextSubmitted += OnTextSubmitted;

        _net.ChatMessage += OnChatMessage;

        // 回放历史：进场景前可能已经收到欢迎语等早到消息，避免错过。
        foreach (var line in _net.ChatHistory)
        {
            OnChatMessage(line.Name, line.Color, line.Text);
        }
    }

    public override void _ExitTree()
    {
        _net.ChatMessage -= OnChatMessage;
    }

    public override void _UnhandledInput(InputEvent evt)
    {
        // 回车：未聚焦时打开输入框；聚焦时由 LineEdit 自行处理发送。
        if (Input.IsActionJustPressed("ui_accept") && !_input.HasFocus())
        {
            _input.GrabFocus();
            GetViewport().SetInputAsHandled();
        }
        // Esc：退出输入框。
        else if (Input.IsActionJustPressed("ui_cancel") && _input.HasFocus())
        {
            _input.ReleaseFocus();
            GetViewport().SetInputAsHandled();
        }
    }

    private void OnTextSubmitted(string text)
    {
        string trimmed = text.Trim();
        if (!string.IsNullOrEmpty(trimmed))
        {
            _net.SendChatMessage(trimmed);
        }
        _input.Text = "";
        _input.ReleaseFocus();
    }

    private void OnChatMessage(string name, Color color, string text)
    {
        GD.Print($"[聊天][{name}] {text}");
        string hex = color.ToHtml(); // rrggbb
        _log.AppendText($"[color=#{hex}]{name}[/color]: {text}\n");
    }
}
