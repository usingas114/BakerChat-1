# BakerChat · 本地版

基于《明日方舟：终末地》世界观的 AI 角色聊天应用。
**本地运行 · API Key 加密存储 · 直连 Agnes API**

---

## 快速开始

### 1. 获取 API Key

访问 [api.agnes-ai.cn](https://api.agnes-ai.cn/) 注册账号，获取 API 密钥（`sk-` 开头）。

### 2. 运行

双击 `BakerChat.exe`。

- **首次运行**：自动打开浏览器到配置页面 `http://127.0.0.1:8000/setup`
- **输入 API Key** → 选择服务区（推荐 `.cn 国区`）→ 选择模型（推荐 `agnes-2.5-flash`）
- **点击"验证并保存"** → 保存成功自动跳转到聊天界面
- **之后每次运行**：直接进入聊天界面

### 3. 开始聊天

- 左侧选择一位干员（共 29 位）
- 底部输入框输入消息
- AI 会以该干员的口吻回复

---

## 功能

- ✅ 29 位干员，各有独立的角色提示词
- ✅ 完整的对话上下文（多轮记忆）
- ✅ API Key 加密存储在 `config/settings.dat`，不会上传
- ✅ 完全本地运行，数据不离开你的电脑
- ✅ 使用原版 `chat.peilika.beer` 的前端 UI

---

## 目录说明

```
BakerChat/
├── BakerChat.exe          主程序
├── prompts/               角色提示词（29 个 .md）
│   ├── _default.txt       可选：默认提示词（找不到角色时用）
│   ├── _rules.txt         可选：通用规则（追加到所有角色后）
│   └── {角色名}.md        每个角色的提示词
├── web/                   前端文件（原版 UI）
└── config/                配置目录（首次运行自动创建）
    └── settings.dat       加密的 API Key
```

---

## 自定义

### 修改角色提示词

直接编辑 `prompts\{角色名}.md`，保存后**重启程序**生效。

### 添加通用规则

创建 `prompts\_rules.txt`，内容会追加到**所有**角色提示词后面。
例如统一要求角色回复带括号描写：

```
### 回复风格规则
1. 每条回复必须包含括号内的描写，如(挑眉)、(指尖敲了敲桌面)
2. 台词约占三分之二，描写约占三分之一
3. 禁止只输出括号描写而没有台词
```

### 换模型

编辑 `config/settings.dat` 需要重新配置。可以删除它，重启程序会重新弹出 `/setup` 页面。

---

## 常见问题

**Q: 提示"尚未配置 API Key"？**
A: 访问 [http://127.0.0.1:8000/setup](http://127.0.0.1:8000/setup) 配置。

**Q: 回复很慢/失败？**
A: 检查网络能访问 `api.agnes-ai.cn`。可以先用命令行测试：
```
curl -H "Authorization: Bearer 你的Key" https://api.agnes-ai.cn/v1/models
```

**Q: 端口 8000 被占用？**
A: 编辑 `src/main.cpp` 改端口，重新编译。

**Q: 怎么卸载？**
A: 删除整个文件夹即可。没有注册表、没有系统目录写入。

---

## 免责声明

- 本工具为**第三方同人作品**，与鹰角网络、终末地官方无关
- 使用的角色立绘、世界观设定版权归鹰角网络所有
- **仅供学习交流**
- 使用产生的任何后果由使用者自行承担

---

## 开源

本项目基于原作者 [NCreeper233/endfield-baker-chat](https://github.com/NCreeper233/endfield-baker-chat) 的前端资源构建。

C++ 本地版源码："工作目录\BakerChat\src"目录下
前端资源版权归属原作者bilbil@Nuclear_Creeper所有。
