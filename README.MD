# CloudMusicWebPlayer-Qt

> [!IMPORTANT]  
> 该仓库的代码全部由AI书写，本人对AI的产出质量不做任何保证，仅为WEB应用封装，包括此 README 也是由 AI 书写

⚠️ **法律声明**: 本项目为开源学习项目，非网易云音乐官方产品，与网易公司无任何关联。使用本项目请遵守网易云音乐服务条款。

一个基于 **Qt WebEngine** 的网易云音乐 Web 播放器（非官方），把网页版播放器嵌入到 `QWebEngineView` 中，支持托盘控制（播放/上一曲/下一曲）、播放状态持久化与恢复、关闭到托盘等常用桌面行为。

---

### 功能概览
- **嵌入网易云网页版播放器**（`https://music.163.com/st/webplayer`）。
- **系统托盘控制**：打开主窗口、播放/暂停、上一曲、下一曲、退出、关闭行为选择（隐藏到托盘 / 直接退出）。
- **播放状态持久化**：定期读取页面播放状态（id、播放时间、是否暂停），写入本地 `player_state.json`，页面加载完成后自动恢复播放进度。
- **支持 Shadow DOM 的元素点击**：通过在 `QWebEnginePage` 上注入 JS，按优先级尝试多个选择器并支持 Shadow DOM，模拟鼠标事件实现托盘控制。
- **可配置的应用数据目录**：使用 `QStandardPaths::AppDataLocation` 存储缓存、cookie、持久化数据。

### 快速开始
```bash
# 构建
mkdir build && cd build
cmake ..
make

# 运行
./cloudmusic-web-player-qt
```

---

### 目录结构
```
/project-root
├─ main.cpp
├─ favicon.png
├─ CMakeLists.txt
└─ README.md
```

---

## 依赖与要求
- **Qt 6.x**（需包含 Qt WebEngine 模块：`QtWebEngineWidgets` / `QtWebEngine`）。
- C++11 或更高。
- 在 Linux 上打包时需要系统的 Qt6 WebEngine 运行时库（或静态打包）。
- 推荐安装 `cmake` 用于构建。


---

## 配置与自定义

### 应用数据目录
程序使用 `QStandardPaths::AppDataLocation` 创建数据目录，默认路径示例：
- Linux: `~/.local/share/NeteaseWebPlayer`（具体路径依平台而异）
  数据目录下包含：
- `storage/`：WebEngine 持久化存储
- `cache/`：HTTP 缓存
- `player_state.json`：播放状态持久化文件

### 修改图标
将 `resources/favicon.png`（或可执行目录下的 `favicon.png`）替换为你想要的图标，程序会在启动时加载该文件作为窗口与托盘图标。

### 修改用户代理
在 `main.cpp` 中创建 `QWebEngineProfile` 时设置了 `setHttpUserAgent(...)`，可按需修改为自定义 UA。

### 关闭行为（默认）
默认 **关闭到托盘**（`closeToTray = true`）。可在程序运行时通过托盘菜单切换，设置保存在 `QSettings`（组织名与应用名由 `QApplication::setOrganizationName` / `setApplicationName` 指定）。


---

## 状态持久化细节
- 程序每 4 秒读取页面状态（通过注入 `js_read_state`），并把 JSON 写入 `player_state.json`。
- 页面加载完成后会读取该文件并注入 `js_restore_state_template` 来恢复播放时间与播放/暂停状态。
- `player_state.json` 中还会写入 `saved_at` 字段用于调试或检查最后保存时间。

---

## 常见问题与排查
- **页面无法加载或被重定向**：程序会在 `urlChanged` 回调中检测 host 是否为 `music.163.com`，若不是会重新加载播放器页面。若仍然无法访问，请检查网络或是否被站点限制（需要登录/地区限制）。
- **托盘按钮点击无效**：JS 选择器可能随网易云页面更新而失效。可在 `clickPlayerButton` 的选择器列表中添加或调整选择器，或在浏览器开发者工具中定位正确的元素选择器。
- **播放状态无法恢复**：检查 `player_state.json` 是否存在且格式正确；确认页面中存在 `<audio>` 元素或页面提供的 `window.player` API。
- **缺少 Qt WebEngine 运行时**：在目标机器上需要相应的 Qt WebEngine 库，打包时请包含这些依赖或使用系统包管理器安装。

---

## 贡献
欢迎提交 issue 或 PR：
- 修复选择器以适配网易云页面更新
- 改进状态恢复逻辑（更鲁棒的时间/ID 匹配）
- 增加更多托盘快捷操作（音量、喜欢/取消喜欢等）
  请在 PR 中附带可复现步骤与测试说明。

---

## 许可证
本项目基于 MIT 开源许可证，详情请见 LICENSE 文件。

---