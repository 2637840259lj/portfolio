using System;
using Godot;

/// <summary>
/// 身份层抽象。
///
/// 设计动机：将来如果你把游戏上架 Steam，玩家的“唯一身份”会变成 SteamID64 +
/// Steam 个人昵称（persona），而不是现在离线模式的“昵称 + 房间密码”。
/// 把身份抽象成接口后，上 Steam 时只需新增一个 <see cref="SteamIdentityProvider"/>
/// 实现并在 Bootstrap 里换掉，游戏主体逻辑（昵称显示、重名处理、玩家档案）完全不用动。
///
/// 两种模式的“重名”处理思路不同：
///   - 离线模式（OfflineIdentityProvider）：昵称就是玩家在房间里的唯一标识，
///     服务器强制唯一，重名直接拒绝（见 NetworkManager.RequestJoin）。
///   - Steam 模式（将来）：身份 = SteamID（保证全局唯一），显示名允许重复，
///     重名时自动加 "#1234" 之类的后缀消歧，玩家体验更顺。
/// </summary>

/// <summary>玩家的稳定身份。StableId 在同一次联机会话内唯一。</summary>
public readonly struct Identity
{
    /// <summary>稳定唯一 ID。离线模式用 GUID；Steam 模式用 SteamID64。</summary>
    public readonly string StableId;

    /// <summary>展示名（昵称 / persona 名）。</summary>
    public readonly string DisplayName;

    public Identity(string stableId, string displayName)
    {
        StableId = stableId;
        DisplayName = displayName;
    }
}

/// <summary>
/// 身份提供方接口。客户端用它生成自己的身份，再随加入请求发给服务器。
/// </summary>
public interface IIdentityProvider
{
    /// <summary>返回本机玩家身份。nickname 来自主菜单输入。</summary>
    Identity GetIdentity(string nickname);
}

/// <summary>
/// 离线身份：昵称即身份（同房间内必须唯一，由服务器校验）。
/// StableId 用随机 GUID，仅用于本地追踪，真正判重看的是昵称。
/// </summary>
public class OfflineIdentityProvider : IIdentityProvider
{
    public Identity GetIdentity(string nickname)
    {
        return new Identity(Guid.NewGuid().ToString("N"), nickname.Trim());
    }
}

/// <summary>
/// Steam 身份（占位骨架，未接入 GodotSteam 时不可用）。
/// 接入方式：安装 GodotSteam GDExtension，在 GetIdentity 里读取
/// Steamworks.SteamUser.GetSteamID().ToString() 作为 StableId，
/// 用 SteamFriends.GetPersonaName() 作为 DisplayName。
/// 重名消歧在 NetworkManager 的昵称注册处完成（StableId 唯一，显示名加后缀）。
/// </summary>
public class SteamIdentityProvider : IIdentityProvider
{
    public Identity GetIdentity(string nickname)
    {
        // TODO: 接入 GodotSteam 后替换。现在回退到离线行为，便于无 SDK 时编译通过。
        GD.PrintErr("[SteamIdentityProvider] 尚未接入 GodotSteam，临时回退到离线身份。");
        return new Identity(Guid.NewGuid().ToString("N"), nickname.Trim());
    }
}
