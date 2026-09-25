# Coding

一个面向 Windows OIer 的**编译 + 运行**工具：编译一次，然后把每个测试点的标准输入 /
标准输出重定向到文件，并报告退出码、CPU 时间与峰值内存。

> **它不判题。** 

## 构建

```sh
cmake -S . -B build
cmake --build build --config Release
```

三个可执行文件会直接生成在仓库根目录：`cg_b.exe`、`cg_s.exe`、`cg_i.exe`。

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
| `[compiler].output` | `cg_b` 用 | 必填 |
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

复制 `config.example.toml` 为 `config.toml` 再改。相对路径按**配置文件所在目录**解析。

| 键 | 必填 | 说明 |
| --- | --- | --- |
| `[compiler].source` | ✔ | 源文件 |
| `[compiler].output` | ✔ | `cg_b` 用的原版可执行文件输出路径 |
| `[compiler].output_probe` | | `cg_s` / `cg_i` 用的注入版输出路径；缺省由 `output` 派生，必须与 `output` 不同 |
| `[compiler].args` | | 完整编译命令；源码与 `-o <output>` 会自动追加。默认 `g++ -std=c++17` |
| `[inject].enabled` | | 是否给 `cg_s` / `cg_i` 注入 `probe.h`（默认 `true`）|
| `[inject].header` | | 要注入的头文件；缺省按配置文件目录找 `include/inject/probe.h`，找不到就按 `cg_*.exe` 所在目录找 |
| `[runner].work_dir` | ✔ | 运行目录 |
| `[runner].time_limit_ms` | | CPU 时间上限 |
| `[runner].memory_limit_mb` | | 内存上限 |
| `[io].input_dir` | ✔ | 存放 `*.in` 的目录（`-s` / `-i` 模式也要求填写） |
| `[io].output_dir` | ✔ | 存放 `*.out` 的目录 |
| `[io].single_input` | | `cg_s` 的输入文件 |
| `[io].single_output` | | `cg_s` / `cg_i` 询问保存时的目标文件 |
| `[io].colorize_output` | | `cg_s` / `cg_i` 是否给程序自身输出染色（默认 `true`）|
| `[thread].thread_max` | | 并发测试点数，同时受 CPU 核心数限制 |