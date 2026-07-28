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
- 重型构建、慢速认证和正式可见 PIE 串行执行；轻量 NullRHI 自动化最多两个并行，并使用各自工作树的绝对项目路径和唯一日志。
- 功能分支只合并集成工作树更新后的 `master`，不直接互相合并；不得从功能工作树推送或移动 `master`。
- 不使用 `git add .`、`git reset --hard`、`git clean -fdx`、强制覆盖未知改动或共享 `git stash` 交接工作。

若任务需要越过所有权、修改稳定契约、共享资产或默认绑定，停止功能实现并把需求交给集成工作树处理。
