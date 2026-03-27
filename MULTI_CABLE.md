# BlackHole Multi-Cable

该 fork 在官方 [BlackHole](https://github.com/ExistentialAudio/BlackHole) 基础上添加了多虚拟音频线支持：一次编译安装可得到多根独立的 2ch 虚拟音频线，并自动将它们聚合为一个 Aggregate 设备，方便接入 Logic Pro、AU Lab 等宿主软件。

## 新增文件

| 文件 | 说明 |
|------|------|
| `cables.txt` | 配置文件，每行一个音频线后缀名 |
| `build_and_install.sh` | 编译并安装所有音频线 + 创建聚合设备 |
| `uninstall_all.sh` | 卸载所有音频线和聚合设备 |
| `create_aggregate_device.c` | CoreAudio 工具：将所有 `BlackHole_*` 设备聚合 |

## 使用方法

### 1. 配置音频线

编辑 `cables.txt`，每行一个名称后缀（注释行以 `#` 开头）：

```
# BlackHole Multi-Cable Configuration
netease_music
chrome
vlc
other_browser
video_player
```

每个后缀会生成一个独立的 2ch 虚拟音频设备，例如 `BlackHole_netease_music`。

### 2. 编译并安装

```bash
cd /path/to/BlackHole
./build_and_install.sh
```

脚本会：
1. 卸载所有已存在的 `BlackHole*.driver`
2. 用 `clang` 编译每根音频线（无需完整 Xcode，仅需 Command Line Tools）
3. 安装到 `/Library/Audio/Plug-Ins/HAL/`
4. 重启 `coreaudiod`
5. 自动创建 **BlackHole_Aggregate** 聚合设备，包含全部音频线

> 需要 sudo 权限（安装驱动时会提示输入密码）

### 3. 验证

```bash
system_profiler SPAudioDataType | grep -E "BlackHole"
```

应看到所有 `BlackHole_*` 设备和 `BlackHole_Aggregate`。

### 4. 在 Logic Pro 中使用

打开 Logic Pro → 菜单 **Logic Pro → 设置 → 音频** → 将输入设备设为 **BlackHole_Aggregate**。

各路音频的路由：

| 应用/场景 | 输出设备 |
|----------|---------|
| 网易云音乐 | BlackHole_netease_music |
| Chrome | BlackHole_chrome |
| VLC | BlackHole_vlc |
| 其他浏览器 | BlackHole_other_browser |
| 视频播放器 | BlackHole_video_player |

Logic 通过聚合设备可同时接收所有 5 路，每路占用 2 个通道（共 10ch）。

### 5. 卸载

```bash
./uninstall_all.sh
```

会移除聚合设备、所有 `BlackHole_*` 驱动，并重启 coreaudiod。

## 增加 / 修改音频线

编辑 `cables.txt` 后重新运行 `./build_and_install.sh` 即可。旧的驱动会被自动替换。

## 注意事项

- **需要 Command Line Tools**（`xcode-select --install`），不需要完整 Xcode
- **聚合设备在重启后会消失**（macOS 限制，通过 API 创建的临时聚合设备不持久）。重启后重新运行 `./build_and_install.sh` 即可重建（已安装的 `.driver` 不受影响，脚本会直接跳过编译步骤等待 coreaudiod 重启后重建聚合）
- **代码签名**：本地安装无需签名；如需分发安装包，参考官方 `Installer/create_installer.sh`

## 技术原理

BlackHole 的 `BlackHole.c` 使用 `#ifndef` 宏守卫暴露全部关键参数：

```c
#ifndef kDriver_Name
#define kDriver_Name "BlackHole"
#endif
#ifndef kNumber_Of_Channels
#define kNumber_Of_Channels 2
#endif
```

`build_and_install.sh` 对每根音频线传入不同的 `-D` 预处理参数并生成唯一 UUID，使多个独立驱动共存互不干扰。
