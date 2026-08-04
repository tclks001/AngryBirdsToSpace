# Angry Birds To Space 仓库协作规则

开始任何代码、资产、配置或文档修改前，必须完整阅读：

- `Docs/ABTSMultiWorktreeDevelopmentGuide.md`

本仓库采用一个原始集成工作树和 M3、M7、M11 三个功能工作树。会话必须先用
`git rev-parse --show-toplevel`、`git branch --show-current`、`git status --short`
确认自己的工作树身份和状态，再按规范中的所有权范围工作。

强制规则：

- `master`、集成候选、稳定契约、共享热点、共同地图、本文件及多工作树规范只由原始集成工作树修改。
- M3、M7、M11 功能工作树不得修改 `AGENTS.md` 或
  `Docs/ABTSMultiWorktreeDevelopmentGuide.md`。
- 同一 `.uasset` 或 `.umap` 在一个集成周期内只有一个写入者；二进制冲突必须回到唯一基线后在 Unreal Editor 中重放。
- 多工作树期间禁用 Live Coding 和 Hot Reload。完整链接前关闭当前工作树自己的 Editor，不得结束其他工作树或用户已有的 Unreal 进程。
- 本项目唯一允许的 Unreal Engine 是 `C:\Program Files\Epic Games\UE_5.8`；所有编译、UHT、自动化、Standalone 和 Editor/PIE 命令都必须从该绝对路径派生，不得使用源码版或其他 UE 5.8 安装。
- 只有用户在当前任务中明确要求代为控制电脑、操作 Unreal Editor 或执行可见 PIE 时，才允许 GUI/电脑控制；“实现”“编译验证”或“说明 PIE 验收”不构成 GUI 授权。未获授权时只给出严格的 Editor/PIE 步骤与验收标准，不启动图形化 Editor。
- 重型构建、慢速认证和正式可见 PIE 串行执行；轻量 NullRHI 自动化最多两个并行，并使用各自工作树的绝对项目路径和唯一日志。
- M3、M7、M11 开发中必须持续更新各自排错账本；功能交接列出新增/更新 ID。集成工作树在合并时按摘录基线提炼到 `Docs/DevelopmentTroubleshooting.md`，并永久保留三份原始账本。
- 编辑器预览、Preview/Test、生产消费、NullRHI、实时 Chaos 和可见 PIE 是不同证据层；不得互相代替。结果身份应记录 Seed、版本、Profile、Authority 与 Candidate/Result Hash，失败必须 fail closed。
- 功能分支只合并集成工作树更新后的 `master`，不直接互相合并；不得从功能工作树推送或移动 `master`。
- 不使用 `git add .`、`git reset --hard`、`git clean -fdx`、强制覆盖未知改动或共享 `git stash` 交接工作。

若任务需要越过所有权、修改稳定契约、共享资产或默认绑定，停止功能实现并把需求交给集成工作树处理。
