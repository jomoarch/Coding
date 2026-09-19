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
| `[compiler].output` | ✔ | 可执行文件输出路径 |
| `[compiler].args` | | 完整编译命令；源码与 `-o <output>` 会自动追加。默认 `g++ -std=c++17` |
| `[runner].work_dir` | ✔ | 运行目录 |
| `[runner].time_limit_ms` | | CPU 时间上限 |
| `[runner].memory_limit_mb` | | 内存上限 |
| `[io].input_dir` | ✔ | 存放 `*.in` 的目录（`-s` / `-i` 模式也要求填写） |
| `[io].output_dir` | ✔ | 存放 `*.out` 的目录 |
| `[io].single_input` | | `cg_s` 的输入文件 |
| `[io].single_output` | | `cg_s` / `cg_i` 询问保存时的目标文件 |
| `[io].colorize_output` | | `cg_s` / `cg_i` 是否给程序自身输出染色（默认 `true`）|
| `[thread].thread_max` | | 并发测试点数，同时受 CPU 核心数限制 |