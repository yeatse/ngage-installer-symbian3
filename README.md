# 在 Symbian^3 手机上玩 N-Gage 2.0

把 Nokia N97 的 N-Gage 2.0 适配到已破解的 Symbian^3 / Belle 手机，
让没有实体键盘的机型也能用触屏玩游戏。当前版本：**1.30(1550)**。

已在 **Nokia 808 Belle FP2** 上实测 Hooked: Creatures of the Deep，
游戏画面、声音、虚拟按键和手机音量键均可正常使用。其他游戏和机型尚未逐一验证。

## 做了什么

- 保留原来的 **Games**：在里面安装游戏、点击封面启动。
- 加入可隐藏的半透明虚拟键：左侧五向键，右侧 R / A / B / L，左上角展开或收起。
- 适配游戏显示和声音，支持用手机音量键调整音量。
- 不增加额外的 Games(Touch) 图标，新版不再生成诊断日志。

适配以 N97 版程序为基础，为新系统补齐显示、声音和触控操作的兼容处理，
不需要刷入新 ROM，也不需要修改游戏文件。

## 下载与使用

手机需要已经破解，并开启 **Install Server** 和 **Open4All**。

1. 打开 [安装包下载页面](https://github.com/yeatse/ngage-installer-symbian3/actions/workflows/build.yml)，选择最新成功的构建，在底部 **Artifacts** 下载 `ngage-installer-symbian3-unsigned` 并解压。下载需要登录 GitHub。
2. 将 **`ngage-installer-symbian3.sis`** 传到手机安装，无需签名。普通手机请使用这个文件。
3. 把 `.n-gage` 游戏文件放入手机的 **`E:\n-gage`** 文件夹。
4. 打开 **Games**，在其中安装游戏，再点击游戏封面启动。

升级时，请先退出 Games，覆盖安装后重启手机。安装包不包含游戏，游戏文件需自行准备。

## 效果截图

以下截图来自 X7 ROM 的 EKA2L1 模拟器验证；上面的 808 实测结果由用户反馈确认。

**在 Games 内完成游戏安装**

![Games 内提示游戏安装完成](validation/screenshots/x7-install-complete.png)

**游戏画面与虚拟按键：R 在右上，L 在右下，左上角可收起按键**

![Hooked 游戏画面及五向键、R、A、B、L 和收起按钮](docs/images/game-controls.png)

## 更多资料

- [适配原理与改动总结](docs/adaptation-summary.zh-CN.md)
- [从源码构建安装包](docs/build.md)
- [验证过程、问题排查与历史记录](validation/README.md)

Nokia 程序和游戏画面的版权归原权利人所有，项目的 MIT 许可不涵盖这些内容。
