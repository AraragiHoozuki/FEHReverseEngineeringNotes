# FEH PvE 敌人 Engage 使用说明

先准备已经给固定敌人写好 Engage Hero PID 和等级的 PvE 地图文件，并按原地图名替换到游戏实际读取的位置；然后在运行 FEH 的那个已 root 雷电实例中安装 `feh-engage-lsposed-loader.apk`，用 MT 管理器等 root 文件管理器创建 `/data/local/tmp/feh-engage/`，把 `libfeh_engage.so` 和 `feh-engage-native-bridge.jar` 放进去，文件名不要改，并把 `.so` 权限设为 `755`、`.jar` 设为 `644`；接着打开 LSPosed，启用 `FEH Engage Loader`，作用域只勾选 `com.nintendo.zaba`，最后在 Android 设置或最近任务中彻底强行停止 FEH 后重新打开并进入目标 PvE 地图即可。若未生效，优先确认操作的是正确的雷电实例、地图确实已替换、两个文件路径和权限正确，以及 APK、`.so`、`.jar` 来自同一套模块文件；当前只支持地图开场即存在的固定敌人，不支持增援敌人。
