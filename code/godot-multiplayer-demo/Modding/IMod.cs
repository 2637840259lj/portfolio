using System;

/// <summary>
/// 模组系统接口定义。
///
/// 设计目标：像“我的世界”那样，服务端和客户端都能加载模组/插件，
/// 且模组能挂钩游戏的关键事件（玩家加入/离开、聊天、每帧更新等）。
///
/// 使用方式：
///   1. 内置模组：在 *本项目* 里写一个实现 IMod 的类即可，ModLoader 会自动发现并加载
///      （示例见 SampleMod.cs）。
///   2. 外部模组：把编译好的 .dll 放进 user://mods/ 或 res://mods/，
///      dll 里只要有实现 IMod 的类型，ModLoader 会在运行时 Assembly.LoadFrom 加载。
///      （外部 dll 需用与本游戏相同的 Godot / .NET 版本编译。）
///
/// 模组用 [ModSide] 声明作用域，避免把“只该跑在服务端”的逻辑误加载到客户端：
///   - Server：只在专用/主机服务器加载（如经济系统、管理指令）。
///   - Client：只在客户端加载（如 HUD、贴图替换）。
///   - Both  ：两端都加载（如通用命令、聊天过滤）。
/// </summary>

/// <summary>模组作用域，决定该模组在哪种运行时被加载。</summary>
public enum ModSide
{
    Both,
    Server,
    Client,
}

/// <summary>声明模组作用域的特性，ModLoader 据此过滤。</summary>
[AttributeUsage(AttributeTargets.Class)]
public class ModSideAttribute : Attribute
{
    public ModSide Side { get; }
    public ModSideAttribute(ModSide side) => Side = side;
}

/// <summary>一个模组。游戏启动时由 ModLoader 实例化并调用 Init。</summary>
public interface IMod
{
    /// <summary>模组唯一 ID（建议小写带作者前缀，如 "com.example.welcome"）。</summary>
    string Id { get; }

    /// <summary>初始化：在这里订阅 loader 上的事件钩子。</summary>
    /// <param name="loader">ModLoader 实例，提供事件与辅助 API。</param>
    void Init(ModLoader loader);
}
