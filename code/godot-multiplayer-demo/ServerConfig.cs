using System;
using System.Collections.Generic;
using System.IO;
using Godot;

/// <summary>
/// 极简服务器配置文件解析（类 ini，key=value，# 开头为注释）。
///
/// 读取顺序：优先 user://server.cfg（可写，方便部署时改），
/// 不存在再读 res://server.cfg（随包发布的内置默认）。
/// 命令行参数（--port / --max-players）优先级高于配置文件，见 Bootstrap.cs。
///
/// 示例 server.cfg：
///   # 房间密码，空着就是没密码
///   password=1234
///   max_players=16
///   port=7777
///   # 逗号分隔的管理员昵称（仅专用服务器模式生效；主机模式房主恒为管理员）
///   admin=Admin,Bot1
/// </summary>
public partial class ServerConfig : Node
{
    public string Password { get; private set; } = "";
    public int MaxPlayers { get; private set; } = 0; // 0 = 用代码默认
    public int Port { get; private set; } = 0;       // 0 = 用代码默认
    public string Admin { get; private set; } = "";   // 逗号分隔的管理员昵称

    public void Load()
    {
        string text = null;
        // user:// 优先（部署后可改），其次 res:// 内置。
        if (Godot.FileAccess.FileExists("user://server.cfg"))
        {
            using var f = Godot.FileAccess.Open("user://server.cfg", Godot.FileAccess.ModeFlags.Read);
            text = f?.GetAsText();
        }
        else if (Godot.FileAccess.FileExists("res://server.cfg"))
        {
            using var f = Godot.FileAccess.Open("res://server.cfg", Godot.FileAccess.ModeFlags.Read);
            text = f?.GetAsText();
        }

        if (string.IsNullOrEmpty(text))
        {
            GD.Print("[ServerConfig] 未找到配置文件，使用内置默认值。");
            return;
        }

        var dict = Parse(text);
        if (dict.TryGetValue("password", out string pw)) Password = pw;
        if (dict.TryGetValue("max_players", out string mp) && int.TryParse(mp, out int mpi)) MaxPlayers = mpi;
        if (dict.TryGetValue("port", out string pr) && int.TryParse(pr, out int pri)) Port = pri;
        if (dict.TryGetValue("admin", out string ad)) Admin = ad;
        GD.Print($"[ServerConfig] 已加载：password={(string.IsNullOrEmpty(Password) ? "(无)" : "***")}, max_players={MaxPlayers}, port={Port}, admin={Admin}");
    }

    private static Dictionary<string, string> Parse(string text)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        using var reader = new StringReader(text);
        string line;
        while ((line = reader.ReadLine()) != null)
        {
            line = line.Trim();
            if (line.Length == 0 || line.StartsWith("#")) continue;
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            string key = line.Substring(0, eq).Trim();
            string val = line.Substring(eq + 1).Trim();
            result[key] = val;
        }
        return result;
    }
}
