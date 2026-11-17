#pragma once
#include <QString>

// 开机自启管理器。
//
//  1) 本程序清单是 requireAdministrator（vcxproj: UACExecutionLevel），
//     64 位程序写在 HKCU\...\Run 的自启项在登录时拿不到管理员令牌，
//     Windows 会直接跳过它——不提示、不报错，桌面什么都不会出现。
//  2) 自启值不加引号，路径里一旦有空格，登录时 CreateProcess 会在第一个空格处
//     截断命令行，报的正是"找不到文件"。
//  3) 原实现只在设置对话框按"确定"时写一次，写的是"当前正在跑的那份 exe"。
//     从 x64\Debug 里点开设置改一次，自启就永久指向 Debug 目录，之后一清构建/一搬家就废。
//
// 策略：
//  * 首选"计划任务 /SC ONLOGON /RL HIGHEST"：登录即用完整管理员令牌拉起，不弹 UAC；
//  * 兜底才写 HKCU\...\Run，且一定带引号、一定带 --autostart 参数；
//  * 每次启动调用 sync()，把登记项重新指回"当前正在运行的 exe"，自愈。
class AutoStart {
public:
    static bool    isEnabled();                 // 计划任务或 Run 项任一存在
    static bool    enable(QString* err = nullptr);
    static void    disable();
    static bool    sync();                      // 按 Settings::autoStart() 校正登记项
    static QString describe();                  // 一行状态，写日志用

    static void    log(const QString& msg);     // 追加到 startup.log
    static QString logPath();

    static const wchar_t* kTaskName;
    static const wchar_t* kRunValue;
};