# Coding

一个面向 Windows OIer 的**编译 + 运行 + 比较 + 存档**工具：编译一次，把每个测试点的标准输入 /
标准输出重定向到文件，报告退出码、CPU 时间与峰值内存；再把输出与期望答案逐一比较，并把比较结果
存进一个可以浏览、可以管理的历史库。

> **它不判题。**

## 上手

```sh
# 1. 写一份 config.toml（所有键都是可选的，见下面的「配置」；仓库里的 config.toml 就是一份可用示例）
# 2. 编译并跑完 [io].input_dir 下的全部测试点，结果落到 [io].output_dir
cg_b

# 3. 把跑出来的结果与 [io].answer_dir 里的期望答案比一遍，然后进全屏浏览
cmp_b

# 4. 之后随时回看最近一次比较，或者翻历史
view_b
```

单测试点用 `cg_s` + `cmp_s` + `view_s`，交互运行用 `cg_i`。

## 可执行文件一览

十四个 exe 都生成在 `bin/`：

| 工具 | 干什么 | 需要的配置键 |
| --- | --- | --- |
| `cg_b` | 编译一次，跑 `[io].input_dir` 下全部 `*.in` | `[compiler]` `[runner]` `[io].input_dir` `[io].output_dir` |
| `cg_s` | 编译一次，用 `[io].single_input` 跑一次 | 上面那些 + `[io].single_input` |
| `cg_i` | 编译一次，直接在控制台交互运行 | 同 `cg_s` |
| `clean_dir` | 清空 `[io].input_dir`（只删 `*.in`）与 `[io].output_dir` | `[io].input_dir` `[io].output_dir` |
| `cmp_s` | 比较 `single_output` 与 `single_answer`，预览后询问是否存档 | 两个路径（+ `result_root`） |
| `cmp_b` | 比较整个 run，浏览后询问是否存档 | `input_dir` `output_dir` `answer_dir`（+ `result_root`） |
| `view_s` / `view_b` | 只读仓库：打开最新那次比较 / 最新那个 run | `result_root` |
| `rman_s` / `rman_b` | 管理记录：看、删（进回收站）、加/取消保护 | `result_root` |
| `tman_s` / `tman_b` | 管理回收站：恢复、彻底删除、清空 | `result_root` |
| `pin_s` / `pin_b` | 管理保护名单：把记录移出名单 | `result_root` |

**没有任何键是必填的**：加载只负责读文件、填默认值，每个工具自己检查它实际需要的字段。

## 构建

```sh
cmake -S . -B build
cmake --build build --config Release
```

## 编译并运行：`cg_*`

| 可执行文件 | 模式 | 行为 |
| --- | --- | --- |
| `cg_b` | batch | 编译一次，跑 `[io].input_dir` 下的全部 `*.in` |
| `cg_s` | single | 编译一次，用 `[io].single_input` 作为输入跑一次 |
| `cg_i` | interactive | 编译一次，直接在控制台里交互运行（stdin 接到你的键盘） |

三种模式都会重定向 stdout 与 stderr；`cg_i` 之外的模式不会在终端回显程序输出，输出只落盘到对应的
`*.out`。

### 为什么有两份可执行文件

`cg_b` 跑的是**没有注入任何东西**的原版程序，`cg_s` / `cg_i` 跑的是**注入了
`include/inject/probe.h`** 的版本，所以它们必然是两份不同的文件：`[compiler].output` 给 `cg_b` 用，
`[compiler].output_probe` 给 `cg_s` / `cg_i` 用（缺省由 `output` 派生，必须与它不同）。
两者各自判断「是否过期」、按需编译，互不影响。

### stdout / stderr 的分离

两条独立管道**无法**保证 stdout 和 stderr 的先后顺序——先写的那一路可能后到。所以 `cg_s` / `cg_i`
换了个做法：

1. **编译期**：把 `include/inject/probe.h` 强制塞进翻译单元（`g++` / `clang++` 用 `-include`，
   `cl.exe` 用 `/FI`）。它接管 `cout` / `cerr` / `clog` 以及 `printf` 这一族 C 输出函数，把所有输出
   写进**同一条**带标记的流：`\x01` 表示 stdout、`\x02` 表示 stderr，且只在流切换时打一次标记。
2. **运行期**：`cg` 解析这条流，把字节还原成两路——顺序天然就是程序产生的顺序。

于是 `cg_s` / `cg_i` 里 stdout 绿色、stderr 红色，像终端里那样正确交织。

不想注入就用 `[inject].enabled = false`：`cg_s` / `cg_i` 退回去用原版程序加两条独立管道，顺序不再有
保证，但编译更快。

### stderr 要不要进输出文件

`[io].merge_stderr` 决定保存下来的输出文件里是否也包含 stderr：

| 取值 | 效果 |
| --- | --- |
| `true` | stderr 也写进输出文件。注入版下两路在同一流里，**顺序和程序写的一致**；没注入时来自两条管道，顺序不保证 |
| `false` | 输出文件只存 stdout；stderr 直接打到终端，不会丢 |
| 不写 | 各自维持原来的样子：`cg_b` 合并进 `*.out`，`cg_s` / `cg_i` 只存 stdout |

### 交互模式下的粘贴

`cg_i` 会等一次粘贴**整块到齐**再交给程序（约 15 ms 的空闲判定），而不是来一行喂一行。

## 比较：`cmp_*`

比较工具与编译、运行解耦：它们不编译、不跑程序，只看已经存在的文件。

**`cmp_s`** 比较 `[io].single_output` 与 `[io].single_answer`，然后进入全屏预览：顶部是匹配状态与
两侧行数，下面是未匹配行的行号列表（两个数字都右对齐），每行前面还有一个类型标记：

| 标记 | 含义 |
| --- | --- |
| `!` | token 不同 |
| `~` | 只有行内空白不同 |
| `+` | 只在 output 里 |
| `-` | 只在 expect 里 |

预览结束后询问是否**存档**这次比较，名字默认取 `[io].single_input` 的文件名（可用
`[io].single_name` 覆盖）；`--no-save` 跳过询问。

**`cmp_b`** 把 `[io].input_dir` 里的每个用例（`<name>.in`）的 `[io].output_dir/<name>.out` 与
`[io].answer_dir` 里的期望答案比一遍，然后进入用例列表：先选用例，再进去看细节。

- 期望答案用 `<name>.ans` 或 `<name>.out`，**两个都在时用 `.ans`**；程序自己的输出永远只认
  `<name>.out`。
- 用例顺序与 `cg_b` 一致（自然排序，`a2` 在 `a10` 前面）。
- 列表每行是 `序号  名称  状态  未匹配行数`，各列按 `formatter` 的规则对齐（名称列补到最长名称，
  其余列右对齐，列间两个空格），顶部一行汇总是 `N test cases, M differ`。
- 状态是 `matched` / `differ` / `no output` / `no answer` / `failed`。只有前两种能进去看；缺文件或
  比较失败的用例会在进列表前用普通文本说明原因。
- 退出后询问是否把**整个 run** 存档。没比较过的用例也会写进 `manifest.tsv`（只有状态、没有文件），
  所以以后打开这份存档，看到的列表和刚才浏览的完全一样。

> **`cmp_b` 不碰 `[io].output_dir`**：不清理、也不创建空的 `.out`，只读取里面已经跑出来的结果。
> （`cg_b` 会清空它——所以两个目录别指向同一个地方。）

## 查看：`view_*`

只读仓库，不进行比较：

- **`view_s`** 打开**最新那次**比较；`--id <id>` 打开指定的那次。
- **`view_b`** 打开**最新那个 run**；`--run <id>` 打开指定的那个。

两个都支持 `--list` 列出历史、`--prune --keep <n>` 手工清理（会先列出要删的东西再问一次，**永远
不会自动删**）。

## 管理历史：`rman_*` `tman_*` `pin_*`

### 仓库长什么样

```
<result_root>/single/index.tsv      映射：名字 + 时间 -> 文件夹
<result_root>/single/<id>/          一次比较：result.cmp + output.txt + answer.txt + meta.txt
<result_root>/batch/index.tsv       映射：时间 -> run 文件夹
<result_root>/batch/<run-id>/       manifest.tsv + 每个用例一个文件夹
<result_root>/.trash/single|batch/  回收站（结构与上面一样，多一列进入回收站的时间）
```

- **文件夹名就是里面内容的 SHA-256**（取前 16 位十六进制，覆盖 `result.cmp`、`output.txt`、
  `answer.txt`；`meta.txt` 不参与），所以同样的内容只会有一个文件夹。
- **去重只刷新时间**：内容一样时不开新文件夹，也不改已经记下的名字与统计，只把索引里的时间挪到最新。
- 索引是「历史」，文件夹是「内容」；索引每行一次保存事件，最新的一行排在最前面（`--list` 与「最新」
  都靠它，不依赖时钟精度或时区）。
- 写入顺序是「先把文件夹写完再改索引」，中途崩掉最多留一个 `.tmp-*` 垃圾目录，不会出现指向空文件夹
  的记录；索引里有读不懂的行会被跳过并告警，不会让整份历史打不开。
- 存档存的是**比较器规范化之后**的字节（和 `result.cmp` 一致），这样以后重看、重算都对得上。

### 三个管理器

| 工具 | 干什么 |
| --- | --- |
| `rman_s` / `rman_b` | 列出所有存档记录：`Enter` 进去看（就是 `view_s` / `view_b` 的浏览），`d` 删除（进回收站），`p` 加入保护名单，`Shift+p` 取消保护 |
| `tman_s` / `tman_b` | 回收站：`r` 恢复，`d` 彻底删除，`Shift+d` 全部清空 |
| `pin_s` / `pin_b` | 保护名单：`d` 把某条记录从名单里去掉 |

三个工具同一套界面：**最上面一行是状态**（库、条数、保护数、上限、当前过滤/排序），中间是列表，
**最下面一行始终留给命令输入**：

- `:` 进入输入，`Enter` 执行，`Esc` 取消这次输入；输入状态下 `j` / `k` / `q` 都是普通字符。
- 命令：`help`、`find <文本>`（按名字或 id 过滤，空则显示全部）、`sort time|name|unmatched`、
  `pin <id>` / `unpin <id>`、`del <id>`（记录库里是送回收站，回收站里是彻底删除）、`restore <id>`。
- 任何界面下 `q` / `Esc` / `Ctrl+C` 都是强制退出。

### 删除、恢复与保护

- 删除只是把记录移进 `<result_root>/.trash/<s|b>/`，**记录自己的名字、时间、状态、计数一个都不变**，
  只额外记下进入回收站的时间；恢复时按**原来的时间**插回索引，所以它会回到原来的位置，而不是跑到
  最上面。
- 被保护的记录不参与数量上限的自动淘汰，但一样可以被 `d` 删掉（删了也能恢复）。
- 把记录移出仓库时，它也会从保护名单里自动摘掉，不会留下悬空条目。

### 两个上限

都读 `config.toml`：

- `[io].single_max_count` / `[io].batch_max_count`：每个库最多留多少条（保护的不计入）。超了把
  **最旧的未保护记录**送进回收站；在**存档、恢复、取消保护**之后都会检查一次。
- `[io].trash_max_bytes`：回收站最多占多少字节。超了就**删掉最早进回收站的**——只删不压缩，行为
  只有一种；`0` 表示不限（和上面两个计数键一致）。

## 按键速查

通用（所有全屏界面）：

| 按键 | 作用 |
| --- | --- |
| `j` / `k`、`↑` / `↓` | 上下移动光标 |
| `Ctrl+j` / `Ctrl+k`、`Ctrl+↑` / `Ctrl+↓` | 只滚动视野，不动光标 |
| `q` / `Esc` | 退出（批量浏览里进了用例之后，先退回用例列表，见下） |

匹配结果详情（`cmp_s` 预览、`view_s`、`cmp_b` / `view_b` / `rman_b` 里进到某个用例之后）：

| 按键 | 作用 |
| --- | --- |
| `Enter` / `→` | 展开这一行，或跳到下一个不同的 token |
| `Shift+Enter` / `←` | 回到上一个 token，或收回这一行 |
| `c` | 收回当前行 |
| `r` | 全部收回 |
| `Backspace` | 批量浏览里：退回用例列表 |
| `q` / `Esc` | 批量浏览里：等同于 `Backspace`（退回列表）；列表页：退出 |

批量用例列表（`cmp_b` / `view_b` / `rman_b`）：

| 按键 | 作用 |
| --- | --- |
| `Enter` | 进入光标所在的用例（`no output` 之类没有内容的用例进不去） |
| `Backspace`、`q`、`Esc` | 退出整个浏览器（在用例内部时则是退回列表） |

管理器（`rman_*` / `tman_*` / `pin_*`）：移动同上，另有各自的动作（见上一节表格）、`:` 开命令输入。

## 公共选项与退出码

所有工具都支持：

```sh
cg_b                      # 用当前目录的 config.toml
cg_b other/oj.toml        # 指定配置文件（所有工具都可以这样给，或用 -c）
cg_b -f                   # 强制重新编译（仅 cg_*）
cmp_s --no-save           # 只比较、不询问是否存档（仅 cmp_s）
view_b --list             # 列出历史
view_b --run <id>         # 打开指定的那个 run
view_b --prune --keep 3   # 手工清理，只留最新 3 条
```

| 选项 | 说明 |
| --- | --- |
| `-c, --config <path>` | 指定配置文件 |
| `-f, --force` | 即使可执行文件比源码新也重新编译（仅 `cg_*`） |
| `--pause` / `--no-pause` | 强制 / 禁止退出前等待按键 |
| `--list`、`--id` / `--run <id>`、`--prune --keep <n>` | 仅 `view_s` / `view_b` |
| `-h, --help` | 显示帮助 |

**退出码**：

| 码 | 运行类工具（`cg_*`） | 比较 / 查看类工具 | 管理类工具（`rman_*` `tman_*` `pin_*`） |
| --- | --- | --- | --- |
| `0` | 全部测试点正常结束 | 完全一致 | 正常退出 |
| `1` | 有测试点报错 | 有差异（或有记录没结果） | —（不会出现） |
| `2` | 配置 / 编译 / IO 出错 | 配置 / IO 出错 | 配置 / IO 出错 |

**关于「按任意键退出」**：默认只在**本进程独占控制台**（也就是双击 exe）时才等待。在终端、VSCode
任务或 CI 里运行时不再阻塞。需要旧行为就用 `--pause`。

## 配置

复制仓库里的 `config.toml` 改一份自己的，或者从空的开始写——**所有键都是可选的**（理由见上文
「可执行文件一览」）。

### 路径基准与配置文件的链接

- **`base`**（顶层键）：文件里所有相对路径都以它为起点，可以写绝对路径，也可以写相对路径（相对的
  `base` 以配置文件所在目录为起点）。不写 `base` 时，相对路径就按**配置文件所在目录**解析——也就是
  一直以来的行为。`base` 不受自身影响，配置文件本身的路径也不受它影响。
- **`config.link`**：想在多处复用同一份配置，就把配置放在任意位置，然后在 `.exe` 旁边放一个
  `config.link` 文本文件。它**不是 TOML**，内容规则是：

  ```
  D:\oj\my-problem\config.toml
  ```

  第一个非空、不以 `#` 开头的行就是配置文件路径：可以写绝对路径，也可以写相对路径（相对路径以
  `config.link` 所在目录，也就是 `.exe` 所在目录，为起点）。路径两旁多余的空格、成对的引号
  （资源管理器「复制为路径」给出的那种）和 UTF-8 BOM 都会被忽略，所以直接粘贴就能用；写多行时只有
  第一行生效，文件为空或只有注释就等于没有链接。工具查找配置的顺序是：
  1. `-c` 指定的路径（没给就是当前目录下的 `config.toml`）；
  2. **`.exe` 旁边的 `config.toml`**；
  3. **`.exe` 旁边的 `config.link`** 指向的文件。

  用真 symlink 把 `config.toml` 链到别处也可以，那种情况下 Windows 自己就解析了，不需要
  `config.link`（symlink 需要管理员或开发者模式，`config.link` 不需要）。跟着链接找到配置时会在
  stderr 打一行 `[config] using <真实路径> (followed <链接>)`；链接指向的文件不存在会直接报错，不会
  退回到别的配置。

### 全部键

| 键 | 谁需要 | 说明 |
| --- | --- | --- |
| `base`（顶层） | 全部 | 文件里所有相对路径的起点；缺省为配置文件所在目录 |
| `[compiler].source` | `cg_*` | 源文件 |
| `[compiler].output` | `cg_b`（关掉注入时 `cg_s` / `cg_i` 也用） | 原版可执行文件输出路径 |
| `[compiler].output_probe` | `cg_s` / `cg_i` | 注入版输出路径；缺省由 `output` 派生，必须与 `output` 不同 |
| `[compiler].args` | `cg_*` | 完整编译命令；源码与 `-o <output>` 会自动追加。默认 `g++ -std=c++17` |
| `[inject].enabled` | `cg_s` / `cg_i` | 是否注入 `probe.h`（默认 `true`）；关掉就不需要头文件 |
| `[inject].header` | `cg_s` / `cg_i` | 要注入的头文件；缺省按 `base`、配置文件目录、`.exe` 目录依次找 `include/inject/probe.h` |
| `[runner].work_dir` | `cg_*` | 运行目录；每个测试点还会在它下面各占一个子目录 |
| `[runner].time_limit_ms` | `cg_*` | CPU 时间上限；不写就永远不杀超时程序 |
| `[runner].memory_limit_mb` | `cg_*` | 内存上限；`0` 表示不限 |
| `[io].input_dir` | `cg_b` `cmp_b` `clean_dir` | 存放 `*.in` 的目录 |
| `[io].output_dir` | `cg_b` `cmp_b` `clean_dir` | 存放 `*.out` 的目录 |
| `[io].single_input` | `cg_s` | `cg_s` 的输入文件 |
| `[io].single_output` | `cg_s` / `cg_i` / `cmp_s` | 程序输出落盘的位置，也是 `cmp_s` 要比对的一侧 |
| `[io].single_answer` | `cmp_s` | 期望答案；只影响比较 |
| `[io].answer_dir` | `cmp_b` | 按测试点名取期望答案的目录，用 `<name>.ans` 或 `<name>.out`（都在时用 `.ans`） |
| `[io].result_root` | `cmp_*` `view_*` `rman_*` `tman_*` `pin_*` | 比较结果仓库：`<root>/single` 存单次比较，`<root>/batch` 存批量 run |
| `[io].single_name` | `cmp_s` | 存档时用的名字；缺省取 `[io].single_input` 的文件名 |
| `[io].single_max_count` | `cmp_s` `rman_s` | 单次比较库最多留多少条（保护的不计入，`0` 为不限） |
| `[io].batch_max_count` | `cmp_b` `rman_b` | 批量库最多留多少个 run（同上） |
| `[io].trash_max_bytes` | `tman_*` 及其余管理工具 | 回收站最多占多少字节，超了删最早进的（`0` 表示不限） |
| `[io].colorize_output` | `cg_s` / `cg_i` | 是否给程序自身输出染色（默认 `true`） |
| `[io].merge_stderr` | `cg_*` | 是否把 stderr 也写进输出文件；不写就各自维持现状（见上文「stderr 要不要进输出文件」） |
| `[thread].thread_max` | `cg_b` | 并发测试点数，同时受 CPU 核心数限制 |
