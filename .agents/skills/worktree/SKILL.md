---
name: worktree
description: >-
  仅在用户在对话框中显式输入 `/worktree` 斜杠命令时触发。
  日常对话中仅泛泛提及 "worktree" 单词时不要自动激活本技能。
  负责严格的前置未提交代码检查（若工作区有修改则必须阻断）、以 main-<git-id> 规范创建独立工作树目录与分支、
  为新 worktree 的 Release 目录准备 PluginTester.exe 与 Stock.dll 运行环境、并在开发完成时仅执行本地编译 Stock.dll，
  严格禁止自动启动 PluginTester.exe，交由用户手动调试。
---

# TrafficMonitorPlugins Git Worktree 规范工作流

本技能专为 `TrafficMonitorPlugins` C++ 项目设计。通过用户在对话框中输入 `/worktree` 命令显式调用，用于多需求或 Agent 独立任务时安全创建并使用 `git worktree`，实现完全隔离的开发与编译调试环境。

> [!NOTE]
> **触发约定**：本技能必须由用户显式输入 `/worktree` 触发。若用户在日常对话或问答中仅仅提及 "worktree" 词汇（无 `/worktree` 命令意图），不要自动执行创建工作树等自动化流程。

---

## 核心硬性约束

1. **未提交改动安全检查（强制中断）**：
   在执行任何 worktree 操作前，必须先执行 `git status --porcelain`。若存在任何未提交的代码变更或未暂存文件，**必须立即终止流程**，提示用户完成提交后再继续。
2. **分支与路径规范**：
   - 根目录：`D:\Program Files (x86)\NIR\worktree`
   - 目标路径：`D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins\main-<git-id>`
   - 分支名称：`main-<git-id>`（其中 `<git-id>` 为当前 commit 的 8 位短 ID，例如 `main-02491967`）。
   - ⚠️ **严格禁止在分支名中使用斜杠 `/`**（如 `fix/xxx`、`worktree/xxx`）。本机 Git 存在已知缺陷，带斜杠分支操作会静默删除 `.git/refs/heads/` 下的子目录，导致分支丢失！
3. **Release 环境完备性**：
   每个 worktree 的 `bin\x64\Release` 必须具备 `PluginTester.exe`、`Stock.dll` 及运行配置文件，且 `lib\x64\Release` 必须包含 `utilities.lib`，确保既可直接调试又可立即增量编译。
4. **交付阶段禁止自启动测试器**：
   需求完成时，仅调用 MSBuild 进行本地 Release x64 编译生成 `Stock.dll`。**严禁自动拉起 `PluginTester.exe`**，由用户根据需要手动启动测试器进行功能验证。

---

## 执行步骤指引

### 第一阶段：前置代码状态检查 (Pre-flight Check)

执行以下命令检查主仓库状态：
```powershell
git status --porcelain
```
- **若输出不为空（存在 modified, untracked 等改动）**：
  **必须立即停止**后续操作，向用户输出如下提示并等待用户处理：
  > ⚠️ **检测到本地仓库有未提交的改动！**
  > 为了防止代码意外覆盖或丢失，请先提交本地 Git 改动（或执行 `git stash`）后，再继续执行 worktree 流程。
- **若输出为空（工作区干净）**：方可进入第二阶段。

---

### 第二阶段：创建并初始化 Worktree

直接调用项目内置的初始化脚本：
```powershell
powershell -ExecutionPolicy Bypass -File ".agents\skills\worktree\scripts\create-worktree.ps1"
```

该脚本将自动完成：
1. 取 HEAD 的 8 位短提交号（如 `02491967`），若存在同名分支/目录则生成唯一 8 位标识。
2. 在 `D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins` 下创建 `main-<git-id>`。
3. 创建并检出无斜杠的独立分支 `main-<git-id>`。
4. 将主仓库已编译好的 `bin\x64\Release`（含 `PluginTester.exe`、`Stock.dll`、`Stock.ini` 等）与 `lib\x64\Release` 同步到新工作树，确保测试器环境即时可用。

---

### 第三阶段：在 Worktree 中进行需求开发

后续所有代码查看、编辑与改动操作，均在新建的 worktree 目录（即 `D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins\main-<git-id>`）中进行，保持主仓库和其他分支干净无干扰。

---

### 第四阶段：需求完成与编译交付 (Build & Deliver)

需求编码与自检完成后，执行编译脚本：
```powershell
powershell -ExecutionPolicy Bypass -File ".agents\skills\worktree\scripts\build-worktree-stock.ps1" -WorktreePath "D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins\main-<git-id>"
```

该脚本将自动执行：
1. 定位本机 MSBuild（BuildTools / VS2022）。
2. 使用 Release x64 配置增量编译 `Stock.vcxproj`（必要时先编译基础库 `utilities.vcxproj`）。
3. 校验产物 `bin\x64\Release\Stock.dll` 的更新时间与 SHA256。
4. **明确不拉起任何 `PluginTester.exe` 进程**。

#### 交付回复规范
编译成功后，Agent 必须向用户明确说明：
1. 需求改动已完成，且 `Stock.dll` 已在 Worktree 下编译成功。
2. 明确给出产物路径（例如 `D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins\main-<git-id>\bin\x64\Release\Stock.dll`）。
3. 明确提示用户：**根据项目规范，Agent 不会自动启动测试器，请您在需要时手动启动 `PluginTester.exe` 进行调试和测试**。
