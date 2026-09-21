using Godot;

/// <summary>
/// 内置示例模组：证明模组系统真的能挂钩游戏事件。
///
/// - 服务端：有玩家加入时，用系统消息广播一句欢迎语（通过 ModLoader.ServerBroadcast）。
/// - 客户端：启动时在控制台打印一条标记，证明客户端模组也被加载了。
///
/// 它声明为 [ModSide(ModSide.Both)]，所以服务端和客户端都会加载。
/// 想写一个“只跑在服务端”的模组（比如经济/权限系统），把特性改成 Server 即可。
///
/// 外部模组写法完全一样：另起一个项目引用本游戏的 Godot.NET.Sdk，
/// 写一个实现 IMod 的类，编译成 dll 丢进 user://mods/ 就能被加载。
/// </summary>
[ModSide(ModSide.Both)]
public class SampleMod : IMod
{
    public string Id => "dev.sample.welcome";

    public void Init(ModLoader loader)
    {
        loader.PlayerJoined += OnPlayerJoined;
        loader.ClientStarted += OnClientStarted;
    }

    private void OnPlayerJoined(long id, string name)
    {
        // 仅服务端广播，避免客户端也各发一遍。
        if (ModLoader.Instance != null && ModLoader.Instance.Multiplayer.IsServer())
        {
            ModLoader.Instance.ServerBroadcast($"欢迎 {name} 加入服务器！（来自示例模组）");
        }
    }

    private void OnClientStarted()
    {
        if (ModLoader.Instance != null && !ModLoader.Instance.Multiplayer.IsServer())
        {
            GD.Print("[SampleMod] 客户端模组已加载（这是一个内置示例模组）。");
        }
    }
}
