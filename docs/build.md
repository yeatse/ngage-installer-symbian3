# 从源码构建安装包

本页面向开发者。手机安装方法见 [README](../README.md)，
实现细节见 [适配总结](adaptation-summary.zh-CN.md)。

## GitHub Actions

[Build unsigned Symbian installer](https://github.com/yeatse/ngage-installer-symbian3/actions/workflows/build.yml)
使用 [symbian-platform](https://github.com/hpnkv/symbian-platform) 0.3.0，
从源码编译 ARM 组件并打包 unsigned SIS。构建结果上传到
`ngage-installer-symbian3-unsigned` artifact。

大型输入按 SHA-256 单独缓存，每次恢复后校验；缓存命中不会请求源服务器。
常规构建不下载 ROM 或游戏。资源清单位于 `validation/inputs.json`、
`validation/sdk.json` 和 `validation/hacked-x7.json`。

## Linux 本地构建

需要 Python 3.11 或更新版本。按 `validation/sdk.json` 下载并校验 SDK，
解压到独立目录，再执行：

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-build.txt
.venv/bin/python tools/fetch_inputs.py downloads --only n97-ngage.sisx
.venv/bin/python tools/fetch_inputs.py downloads --manifest validation/hacked-x7.json \
  --only ynstallserver-belle-fixture.exe
.venv/bin/python tools/build.py --sdk /absolute/path/to/sdk --installer downloads/n97-ngage.sisx
```

默认输出在 `artifacts/`：

| 文件 | 用途 |
| --- | --- |
| `ngage-installer-symbian3.sis` | 普通已破解手机，包括 808 Belle FP2 |
| `build-report.json` | 普通包的哈希、大小及适配信息 |
| `ngage-installer-x7-custom-rom.sis` | 提供的 X7 定制 ROM 验证环境 |
| `build-report-x7-custom-rom.json` | X7 包的构建报告 |

X7 包额外替换 `C:\sys\bin\YnstallServer.exe`，用于该 ROM 的安装服务兼容。
普通包沿用手机现有的安装服务；不要在 808 上使用 X7 专用包。

## 检查与复现

每次构建都会在 ARM CPU 中执行源码编译的软键适配代码，检查两种输入 ABI、
普通按键回退、寄存器及栈保持。完整工程检查可在构建后执行：

```sh
NGAGE_SDK=/absolute/path/to/sdk \
NGAGE_VALIDATION_INPUTS="$PWD/downloads" \
NGAGE_INSTALL_SERVICE="$PWD/downloads/ynstallserver-belle-fixture.exe" \
  .venv/bin/python -m unittest discover -s tests -v
```

运行时验证、资源来源、模拟器分支及历史问题见
[验证记录](../validation/README.md)。测试通过不代表所有游戏和机型均兼容。

## 版本与材料

1.30(1550) 已移除诊断探针、文件日志和 `--diagnostic` 构建入口；
早期诊断实现可从 Git 历史查看。旧手机日志不会自动删除。

上游 Nokia 安装器、ROM、游戏及解压文件不提交到仓库，
其授权不属于本项目 MIT 许可。构建产物不包含 ROM、游戏或游戏许可证。
