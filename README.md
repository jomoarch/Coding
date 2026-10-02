# Coding

一个面向 Windows OIer 的**编译 + 运行**工具：编译一次，然后把每个测试点的标准输入 /
标准输出重定向到文件，并报告退出码、CPU 时间与峰值内存。

> **它不判题。** 

## 构建

```sh
cmake -S . -B build
cmake --build build --config Release
```

十四个可执行文件会直接生成在 `bin/`：`cg_b.exe`、`cg_s.exe`、`cg_i.exe`、`clean_dir.exe`、
`cmp_s.exe`、`cmp_b.exe`、`view_s.exe`、`view_b.exe`、`rman_s.exe`、`rman_b.exe`、`tman_s.exe`、
`tman_b.exe`、`pin_s.exe`、`pin_b.exe`。

## 三种模式

| 可执行文件 | 模式 | 行为 |
| --- | --- | --- |
| `cg_b` | batch | 编译一次，跑 `[io].input_dir` 下的全部 `*.in` |
| `cg_s` | single | 编译一次，用 `[io].single_input` 作为输入跑一次 |
| `cg_i` | interactive | 编译一次，直接在控制台里交互运行（stdin 接到你的键盘） |

三种模式都会重定向 stdout 与 stderr；`cg_i` 之外的模式不会在终端回显程序输出，
输出只落盘到对应的 `*.out`。

## 两份可执行文件

`cg_b` 跑的是**没有注入任何东西**的原版程序，`cg_s` / `cg_i` 跑的是**注入了
`include/inject/probe.h`** 的版本，所以它们必然是两份不同的文件：

| 键 | 用途 | 缺省 |
| --- | --- | --- |
| `[compiler].output` | `cg_b` 用 | 必须自己指定 |
| `[compiler].output_probe` | `cg_s` / `cg_i` 用 | 由 `output` 派生：`test/bin/solution.exe` → `test/bin/solution.probe.exe` |

两者都会做各自的“是否过期”判断、按需编译：`cg_b` 只编原版，`cg_s` / `cg_i` 只编注
入版，互不影响。

## stdout / stderr 的分离

两条独立管道**无法**保证 stdout 和 stderr 的先后顺序——先写的那一路可能后到。所以
`cg_s` / `cg_i` 换了个做法：

1. **编译期**：把 `include/inject/probe.h` 强制塞进翻译单元（`g++` / `clang++` 用
   `-include`，`cl.exe` 用 `/FI`）。它接管 `cout` / `cerr` / `clog` 以及 `printf`
   这一族 C 输出函数，把所有输出写进**同一条**带标记的流：`\x01` 表示 stdout、
   `\x02` 表示 stderr，且只在流切换时打一次标记。
2. **运行期**：`cg` 解析这条流，把字节还原成两路——顺序天然就是程序产生的顺序。

于是 `cg_s` / `cg_i` 里 stdout 绿色、stderr 红色，像终端里那样正确交织；而
`[io].single_output` 保存的仍然只有 stdout。`cg_b` 不注入，行为与以前一致：stdout 与
stderr 都进同一个 `*.out`。

几个已知边界：

- 绕过 CRT 的裸写（直接 `WriteFile` 到 stdout/stderr、第三方库、宽字符流）没有标记。
  子进程的 stderr 接在另一条管道上，所以标记流上未标记的字节一律算 stdout；那条裸
  stderr 管道的内容会在程序结束后按红色补在最后。
- `printf` / `scanf` / `puts` / `fwrite` 等是**宏替换**，因此 `std::printf(...)` 这种
  加 `std::` 限定的写法会编译失败——用不带限定的写法或 iostream 即可。
- 提示信息（没有换行的 `cout << "n? "`）会在读 stdin 前被刷出去，不会卡在缓冲区里。
- 注入只支持 C++ 源文件；`.c` 会被直接拒绝。

不想注入就用 `[inject].enabled = false`：`cg_s` / `cg_i` 会退回去用原版程序加两条独
立管道，顺序不再有保证，但编译更快。

## 比较、存档与浏览

`cmp_s`、`cmp_b`、`view_s` 和 `view_b` 是比较 / 浏览工具，与编译、运行解耦：它们不编译、不跑程序，
只处理 `[io].single_output`、`[io].single_answer`、`[io].answer_dir` 和 `[io].result_root`。
**注意 `cmp_b` 不会碰 `[io].output_dir`**：它不清理、也不创建空的 `.out`，只读取里面已经跑出来的结果。

### 存档

`[io].result_root` 是**比较结果仓库**，分成两个互不相干的库：

```
<result_root>/single/index.tsv      映射：名字 + 时间 -> 文件夹
<result_root>/single/<id>/          一次比较：result.cmp + output.txt + answer.txt + meta.txt
<result_root>/batch/index.tsv       映射：时间 -> run 文件夹
<result_root>/batch/<run-id>/       manifest.tsv + 每个用例一个文件夹
```

- **文件夹名就是里面内容的 SHA-256**（取前 16 位十六进制，覆盖 `result.cmp`、`output.txt`、
  `answer.txt`，`meta.txt` 不参与）。所以同样的内容只会有一个文件夹。
- **去重只刷新时间**：内容一样时不开新文件夹、也不改已经记下的名字与统计，只把索引里的时间挪到最新。
- 索引是"历史"，文件夹是"内容"；索引每行一次保存事件，最新的一行排在最前面（`--list` 与"最新"都靠它，
  不依赖时钟精度或时区）。
- 写入顺序是"先把文件夹写完再改索引"，中途崩掉最多留一个 `.tmp-*` 垃圾目录，不会出现指向空文件夹的
  记录；索引里有读不懂的行会被跳过并告警，不会让整份历史打不开。
- 存档存的是**比较器规范化之后**的字节（和 `result.cmp` 一致），这样以后重看、重算都对得上。

### 四个工具

- **`cmp_s`** 比较 `[io].single_output` 与 `[io].single_answer`，然后进入全屏预览：顶部是匹配状态与
  两侧行数，下面是未匹配行的行号列表（两个数字都右对齐），每行前面还有一个类型标记：
  `!` token 不同、`~` 只有行内空白不同、`+` 只在 output 里、`-` 只在 expect 里。
  预览结束后询问是否**存档**这次比较，名字默认取 `[io].single_input` 的文件名（可用 `[io].single_name`
  覆盖）；`--no-save` 可以直接跳过询问。
- **`view_s`** 只读仓库，打开**最新那次**比较（`--id <id>` 打开指定的那次，`--list` 列出历史）。
- **`cmp_b`** 把 `[io].input_dir` 里的每个用例（`<name>.in`）的 `[io].output_dir/<name>.out` 与
  `[io].answer_dir` 里的期望答案比一遍——答案是 `<name>.ans` 或 `<name>.out`，两个都在时用 `.ans`；
  程序自己的输出永远只认 `<name>.out`。用例顺序与 `cg_b` 一致（自然排序，`a2` 在 `a10` 前面），
  然后进入用例列表：先选用例，再进去看细节。
  - 列表每行是 `序号  名称  状态  未匹配行数`，各列按 `formatter` 的规则对齐（名称列补到最长名称，
    其余列右对齐，列间两个空格），顶部一行汇总是 `N test cases, M differ`。
  - `j` / `k`（或方向键）移动光标，`Ctrl+j` / `Ctrl+k`（或 `Ctrl+方向键`）只滚动视野，`Enter` 进入
    光标所在的用例，`Backspace` 退回列表（光标回到刚进的那个用例），`q` / `Esc` **全局退出**。
  - 状态是 `matched` / `differ` / `no output` / `no answer` / `failed`。只有前两种能进去看；
    缺文件或比较失败的用例会在进列表前用普通文本说明原因。
  - 退出后询问是否把**整个 run** 存档。没比较过的用例也会写进 `manifest.tsv`（只有状态、没有文件），
    所以以后打开这份存档，看到的列表和刚才浏览的完全一样。
- **`view_b`** 只读仓库，打开**最新那个 run**（`--run <id>` 指定，`--list` 列出历史）。

用例内部的按键：`j` / `k`（或方向键）移动光标，`Enter` / `→` 展开或跳到下一个不同的 token，
`Shift+Enter` / `←` 回到上一个 token 或收回，`c` 收回当前，`r` 全部收回，`Ctrl+j` / `Ctrl+k` 只滚动
视野，`Backspace` 退回用例列表，`q` / `Esc` 全局退出。

### 公共选项

四个工具都支持 `[options] [config.toml]`：`-c/--config`、`--pause`、`--no-pause`、`-h/--help`；
`view_s` / `view_b` 另有 `--list`、`--id` / `--run <id>`、`--prune --keep <n>`（手工清理，会先列出
要删的东西再问一次，永远不会自动删）。退出码 `0` 一致、`1` 不一致、`2` 配置或 IO 出错。

## 记录管理

六个交互式工具，`_s` 管单次比较库、`_b` 管批量 run 库：

| 工具 | 干什么 |
| --- | --- |
| `rman_s` / `rman_b` | 列出所有存档记录：`Enter` 进去看（就是 `view_s` / `view_b` 的浏览），`d` 删除（进回收站），`p` 加入保护名单，`Shift+p` 取消保护 |
| `tman_s` / `tman_b` | 回收站：`r` 恢复，`d` 彻底删除，`Shift+d` 全部清空 |
| `pin_s` / `pin_b` | 保护名单：`d` 把某条记录从名单里去掉 |

三个工具同一套界面：**最上面一行是状态**（库、条数、保护数、上限、当前过滤/排序），中间是列表
（`j` / `k` 或方向键移动，`Ctrl+j` / `Ctrl+k` 只滚视野，光标行反色），**最下面一行始终留给命令输入**：

- `:` 进入输入，`Enter` 执行，`Esc` 取消这次输入；输入状态下 `j` / `k` / `q` 都是普通字符。
- 命令：`help`、`find <文本>`（按名字或 id 过滤，空则显示全部）、`sort time|name|unmatched`、
  `pin <id>` / `unpin <id>`、`del <id>`（记录库里是送回收站，回收站里是彻底删除）、`restore <id>`。
- 任何界面下 `q` / `Esc` / `Ctrl+C` 都是强制退出。

**删除与恢复**：删除只是把记录移进 `<result_root>/.trash/<s|b>/`，**记录自己的名字、时间、状态、
计数一个都不变**，只额外记下进入回收站的时间；恢复时按**原来的时间**插回索引，所以它会回到原来的
位置，而不是跑到最上面。被保护的记录不参与数量上限的自动淘汰，但一样可以被 `d` 删掉（删了也能恢复）。

**两个上限**（都读 `config.toml`）：`[io].single_max_count` / `[io].batch_max_count` 限制每个库存
多少条（保护的不计入，超了把**最旧的未保护记录**送进回收站，在存档、恢复、取消保护后都会检查一次）；
`[io].trash_max_bytes` 限制回收站占多少字节，超了就**删掉最早进回收站的**——只删不压缩，行为只有一种。

## 用法

```sh
cg_b                          # 使用当前目录的 config.toml
cg_b other/oj.toml            # 指定配置文件
cg_b -f                       # 强制重新编译
cg_b --no-pause               # 不等待按键
cg_b --help
```

| 选项 | 说明 |
| --- | --- |
| `-c, --config <path>` | 指定配置文件 |
| `-f, --force` | 即使可执行文件比源码新也重新编译 |
| `--pause` / `--no-pause` | 强制 / 禁止退出前等待按键 |
| `-h, --help` | 显示帮助 |

**退出码**：`0` 全部测试点正常结束，`1` 有测试点报错，`2` 配置 / 编译 / IO 出错。

**关于「按任意键退出」**：默认只在**本进程独占控制台**（也就是双击 exe）时才等待。
在终端、VSCode 任务或 CI 里运行时不再阻塞。需要旧行为就用 `--pause`。

## 配置

复制 `config.example.toml` 为 `config.toml` 再改。

**所有键都是可选的。** 加载只负责读文件、填默认值；每个工具自己去检查它实际需要的字段，
所以只服务某一个工具的配置文件（比如只写一个 `[io].result_root` 的 `view_s` 配置）完全可用，
不会因为缺了别的键而整体加载失败。

### 路径基准与配置文件的链接

- **`base`**（顶层键）：文件里所有相对路径都以它为起点，可以写绝对路径，也可以写相对路径
  （相对的 `base` 以配置文件所在目录为起点）。不写 `base` 时，相对路径就按**配置文件所在目录**
  解析——也就是一直以来的行为。`base` 不受自身影响，配置文件本身的路径也不受它影响。
- **`config.link`**：想在多处复用同一份配置，就把配置放在任意位置，然后在 `.exe` 旁边放一个
  `config.link` 文本文件，里面写它的路径（一行，允许 `#` 注释和空行；相对路径以链接文件所在目录
  为起点）。工具查找配置的顺序是：
  1. `-c` 指定的路径（没给就是当前目录下的 `config.toml`）；
  2. **`.exe` 旁边的 `config.toml`**；
  3. **`.exe` 旁边的 `config.link`** 指向的文件。

  用真 symlink 把 `config.toml` 链到别处也可以，那种情况下 Windows 自己就解析了，不需要
  `config.link`（symlink 需要管理员或开发者模式，`config.link` 不需要）。跟着链接找到配置时会
  在 stderr 打一行 `[config] using <真实路径> (followed <链接>)`；链接指向的文件不存在会直接报错，
  不会退回到别的配置。

| 键 | 谁需要 | 说明 |
| --- | --- | --- |
| `base`（顶层） | 全部 | 文件里所有相对路径的起点；缺省为配置文件所在目录 |
| `[compiler].source` | `cg_b` `cg_s` `cg_i` | 源文件 |
| `[compiler].output` | `cg_b`（关掉注入时 `cg_s` / `cg_i` 也用） | 原版可执行文件输出路径 |
| `[compiler].output_probe` | `cg_s` / `cg_i` | 注入版输出路径；缺省由 `output` 派生，必须与 `output` 不同 |
| `[compiler].args` | `cg_*` | 完整编译命令；源码与 `-o <output>` 会自动追加。默认 `g++ -std=c++17` |
| `[inject].enabled` | `cg_s` / `cg_i` | 是否注入 `probe.h`（默认 `true`）；关掉就不需要头文件 |
| `[inject].header` | `cg_s` / `cg_i` | 要注入的头文件；缺省按配置文件目录找 `include/inject/probe.h`，找不到就按 `cg_*.exe` 所在目录找 |
| `[runner].work_dir` | `cg_b` `cg_s` `cg_i` | 运行目录 |
| `[runner].time_limit_ms` | `cg_*` | CPU 时间上限 |
| `[runner].memory_limit_mb` | `cg_*` | 内存上限 |
| `[io].input_dir` | `cg_b` `cmp_b` `clean_dir` | 存放 `*.in` 的目录 |
| `[io].output_dir` | `cg_b` `cmp_b` `clean_dir` | 存放 `*.out` 的目录 |
| `[io].single_input` | `cg_s` | `cg_s` 的输入文件 |
| `[io].single_output` | `cg_s` / `cg_i` | 询问保存时的目标文件 |
| `[io].answer_dir` | `cmp_b` | 按测试点名取期望答案的目录，用 `<name>.ans` 或 `<name>.out`（都在时用 `.ans`）；只影响比较 |
| `[io].single_answer` | `cmp_s` | 要比对的期望答案；只影响比较，编译与运行不需要它 |
| `[io].result_root` | `cmp_s` `cmp_b` `view_s` `view_b` | 比较结果仓库：`<root>/single` 存单次比较，`<root>/batch` 存批量 run |
| `[io].single_name` | `cmp_s` | 存档时用的名字；缺省取 `[io].single_input` 的文件名 |
| `[io].single_max_count` | `cmp_s` `rman_s` | 单次比较库最多留多少条（保护的不计入，0 为不限） |
| `[io].batch_max_count` | `cmp_b` `rman_b` | 批量库最多留多少个 run（同上） |
| `[io].trash_max_bytes` | 全部管理工具 | 回收站最多占多少字节，超了删最早进的（0 表示一进来就清） |
| `[io].colorize_output` | `cg_s` / `cg_i` | 是否给程序自身输出染色（默认 `true`）|
| `[thread].thread_max` | `cg_b` | 并发测试点数，同时受 CPU 核心数限制 |