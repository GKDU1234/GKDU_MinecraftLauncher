# GDML 命令行插件使用手册（command_lines）

## 一、插件简介

`command_lines.dll` 为 GDML 启动器增加一个**交互式命令行控制台**：

- 在主界面底部新增一个「命令行」按钮；
- 点击后打开一个**独立、非模态**的命令行窗口（不会与主界面产生点击冲突，两个窗口可自由切换）；
- 在窗口下方的输入框输入命令，回车（或点「执行」按钮）即执行，结果输出到上方输出框；
- 支持命令历史记录，自动追加保存；
- 支持启动游戏、Mod 屏蔽/启用、插件管理、配置管理等高级功能。

## 二、安装

1. 把编译好的 `command_lines.dll` 放入启动器的插件目录：
   `%APPDATA%\GDML\plugins\command_lines.dll`
2. 重新打开 `GDML.exe`，日志面板出现「命令行插件已加载」即表示成功。

## 三、使用方法

1. 点击主界面底部的「命令行」按钮；
2. 弹出「GDML 命令行」窗口；
3. 在底部输入框输入命令，按 **回车**（或点「执行」按钮）；
4. 结果实时显示在上方输出框；
5. 关闭窗口（点 ×）只是隐藏，再次点「命令行」按钮会重新显示，历史输出仍在。

### Tab 键补全命令

在输入框输入命令名的前几个字母，按 **Tab** 键即可自动补全：

- 输入 `he` + Tab → 自动补全为 `help `；
- 输入 `cl` + Tab → 补全为 `clear `，再按一次 Tab → 切换为 `cls `（循环切换所有匹配项）；
- 补全不区分大小写（`HE`、`He` 都能补全 `help`）；
- 无匹配时按 Tab 不产生任何变化。

可补全的命令：`help`、`log`、`get`、`set`、`time`、`echo`、`history`、`clear`、`cls`、`about`、`ver`、`launch`、`mods`、`mod`、`plugins`、`openconfig`、`cat`、`open`、`gamedir`、`versions`、`whoami`、`mem`。

## 四、命令列表

### 基础命令

| 命令 | 说明 | 示例 |
|---|---|---|
| `help` | 显示命令帮助 | `help` |
| `log <消息>` | 写入启动器日志面板 | `log 你好 GDML` |
| `get <key>` | 读取设置项 | `get gameDir` |
| `set <key> <value>` | 写入设置项 | `set memory 4096` |
| `time` | 显示当前日期时间 | `time` |
| `echo <文本>` | 回显文本 | `echo hello` |
| `history` | 显示命令历史 | `history` |
| `clear` / `cls` | 清空输出区 | `clear` |
| `about` / `ver` | 显示插件版本信息 | `about` |

### 游戏与下载

| 命令 | 说明 | 示例 |
|---|---|---|
| `launch [版本]` | 启动游戏（空=当前选中版本） | `launch 1.21.11` |
| `versions` | 列出已安装版本 | `versions` |
| `mods` | 列出 mod（含启用/禁用状态） | `mods` |
| `mod disable <名>` | 屏蔽 mod | `mod disable sodium` |
| `mod enable <名>` | 启用 mod | `mod enable sodium` |
| `mem <MB>` | 设置内存（MB） | `mem 4096` |

### 插件与配置

| 命令 | 说明 | 示例 |
|---|---|---|
| `plugins` | 列出已加载插件 | `plugins` |
| `openconfig` | 打开启动器配置文件 | `openconfig` |
| `cat <文件>` | 查看文件内容 | `cat config.json` |
| `open <路径>` | 打开文件/文件夹 | `open mods` |
| `gamedir` | 打开游戏目录 | `gamedir` |
| `whoami` | 显示当前账号 | `whoami` |

### 种子工具

| 命令 | 说明 | 示例 |
|---|---|---|
| `seedmap open/close` | 打开/关闭种子地图窗口 | `seedmap open` |
| `seedfind open/close` | 打开/关闭种子查找窗口 | `seedfind open` |

> 注：`seedmap`/`seedfind` 需配合 `seed_mapAfind.dll` 插件使用。

## 五、命令详解

### 1. help
显示所有可用命令及简要说明。

### 2. log <消息>
把一条消息写入启动器的日志面板和 `launcher.log` 文件。

### 3. get <key>
读取启动器设置。支持以下 key：

| key | 含义 |
|---|---|
| `gameDir` | 游戏目录路径 |
| `username` | 当前账号名 |
| `version` | 当前选中的版本 |
| `modDir` | mod 安装目录 |
| `multiAccounts` | 多账号列表（逗号分隔） |
| `launcherDir` | 启动器数据目录 |

### 4. set <key> <value>
写入启动器设置。支持 `memory`、`username`、`multiAccounts` 等。

### 5. launch [版本]
启动游戏。不带参数启动当前选中的版本，带参数启动指定版本。
```
launch 1.21.11        → 启动 1.21.11
launch                → 启动当前选中的版本
```

### 6. versions
扫描游戏目录下的 `versions` 文件夹，列出所有已安装版本。

### 7. mods / mod disable / mod enable
- `mods`：列出 mod 目录下所有 `.jar` 文件，`[启用]` 表示正常加载，`[禁用]` 表示已被屏蔽；
- `mod disable <名>`：把 `xxx.jar` 重命名为 `xxx.jar.disabled`，游戏就不会加载它；
- `mod enable <名>`：把 `xxx.jar.disabled` 恢复为 `xxx.jar`。

```
mods
mod disable sodium
mod enable sodium
```

> 提示：mod 名可带或不带 `.jar` 后缀，大小写需一致。

### 8. mem <MB>
设置游戏分配的内存（单位 MB）。

### 9. plugins
列出当前已加载的所有插件名（逗号分隔转行显示）。

### 10. openconfig
用系统默认程序打开启动器配置文件 `config.json`。

### 11. cat <文件>
查看文本文件内容并输出到输出区。

### 12. open <路径>
用系统默认方式打开一个文件或文件夹。

### 13. gamedir
打开游戏目录（`.minecraft`）。

### 14. whoami
显示当前登录的账号名。

### 15. time / echo / history / clear / about
- `time`：显示当前日期时间；
- `echo <文本>`：回显文本；
- `history`：显示命令历史；
- `clear` / `cls`：清空输出区；
- `about` / `ver`：显示插件版本。

## 六、命令历史文件

所有输入的命令都会以追加方式记录到插件同目录下的：

```
%APPDATA%\GDML\plugins\command_history.log
```

格式为 `[时间戳] 命令`，该文件使用文件锁避免并发写入冲突。

## 七、注意事项

1. 命令行窗口是**非模态**的，可以同时操作主界面和命令行窗口；
2. 窗口关闭（点 ×）只是隐藏，不会丢失已输入的历史输出；
3. `set` 命令对不支持的 key 会静默忽略；
4. `get` 命令对不存在的 key 返回 `(null)`；
5. `mod disable/enable` 通过重命名 `.jar` 文件实现，不影响文件内容；
6. `launch` 命令要求已登录账号，否则启动会失败。
