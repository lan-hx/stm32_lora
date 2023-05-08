# 浙江大学嵌入式系统(2022-2023春夏)小组作业

使用STM32F103C8T6、ESP32、SX1278实现文本聊天功能，可以拓展文本种类（图片，GPS定位等等），实现多样化的功能。

实现3种文本发送模式，包括定向发送，广播（不包括互联网模式）和定时发送。

master: [![CMake](https://github.com/lan-hx/stm32_lora/actions/workflows/cmake.yml/badge.svg?branch=master)](https://github.com/lan-hx/stm32_lora/actions/workflows/cmake.yml)
dev: [![CMake](https://github.com/lan-hx/stm32_lora/actions/workflows/cmake.yml/badge.svg?branch=dev)](https://github.com/lan-hx/stm32_lora/actions/workflows/cmake.yml)

## 环境配置

### Linux

#### 安装必要的包：cmake, ninja, clang-format, clang-tidy

参考：

```shell
sudo apt install cmake git clang-format clang-tidy ninja-build bzip2
```

#### 安装交叉编译器arm-none-eabi

从ARM官网下载：https://developer.arm.com/downloads/-/gnu-rm

新版没有测试：https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

参考：

```shell
cd /opt
sudo wget -O gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2 'https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2?rev=78196d3461ba4c9089a67b5f33edf82a&hash=D484B37FF37D6FC3597EBE2877FB666A41D5253B'
sudo tar -xf gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2
sudo mv gcc-arm-none-eabi-10.3-2021.10 gcc-arm-none-eabi
```

#### 安装STM32Cube

https://github.com/STMicroelectronics/STM32CubeF1

参考：

```shell
cd /opt
sudo git clone https://github.com/STMicroelectronics/STM32CubeF1.git
```

#### 克隆项目

注意：Drivers、Middlewares文件夹下的所有内容不参与编译。可以在创建项目时在`Code Generator Options`中勾选`Add necessary library files as reference in the toolchain project configuration file`避免复制driver文件。

参考：

```shell
cd ~
git clone https://github.com/lan-hx/stm32_lora.git
cd stm32_lora
```

#### configure

需要指定如下变量：

- STM32_TOOLCHAIN_PATH：交叉编译器位置
- STM32_CUBE_F1_PATH：STM32Cube位置
- CMAKE_BUILD_TYPE：（可选）优化等级：Debug，RelWithDebInfo，Release，MinSizeRel
- OPENOCD_BIN：（可选）指定OpenOCD程序路径，指定后可以使用cmake的`flash`、`debug`目标一键下载或调试

注意：如需更改编译设置（增删文件，更改目录等），需要删掉build文件夹清除缓存。

参考：

```shell
cmake -B build -G Ninja -DSTM32_TOOLCHAIN_PATH:PATH=/opt/gcc-arm-none-eabi -DSTM32_CUBE_F1_PATH:PATH=/opt/STM32CubeF1 -DCMAKE_BUILD_TYPE=Debug
```

#### 编译

参考：

```shell
cmake --build ./build
```

#### 烧录

编译结果在build/*.elf，~~百度OpenOCD即可~~。

参考：

Windows在这里下载：https://github.com/xpack-dev-tools/openocd-xpack/releases/download/v0.11.0-5/xpack-openocd-0.11.0-5-win32-x64.zip

烧录：

```shell
openocd -f 'path-to-stlink-cfg' -c 'program path-to-elf' -c reset -c shutdown
```

例如：

```shell
openocd -f C:/Users/xxx\STM32CubeIDE/workspace/lora/utility/stlink.cfg -c 'program "C:/Users/xxx/STM32CubeIDE/workspace/lora/build/lora.elf"' -c reset -c shutdown
```

GDB调试：

```shell
openocd -f 'path-to-stlink-cfg' [-c 'program path-to-elf'] -c "init;reset init;"

# in gdb-multiarch
target extended-remote :3333
add-symbol-file 'path-to-elf'
dir 'path-to-source'
monitor xxx # 向OpenOCD发送命令，比如reset
```

config: https://github.com/lan-hx/stm32_lora/blob/master/utility/stlink.cfg

### Windows

与Linux相同。

有Visual Studio的clang在类似C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin的位置，ninja在类似C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja的位置。

更推荐MinGW-w64+GCC+LLVM+Ninja+...的大礼包：https://winlibs.com/

推荐稚晖君的教程（需要CLion）：https://zhuanlan.zhihu.com/p/145801160

## FAQ

- 关于API名字问什么这么长：考虑到有人用C语言，所以没法使用命名空间。

- 关于`DATA_LINK_SEMAPHORE`等宏：这些变量只需要暴露给相关使用者（实现、FreeRTOS初始化过程等），在其他使用者处隐藏。

## utility文件夹

- stlink.cfg: 用于STLink的OpenOCD配置文件

## 参与大作业须知

本次大作业所有代码均在GitHub上，提交作业需要将自己的代码合并进入主分支，不接受其他方式。你可以Fork + Pull Request，或者新建自己的分支、提交并Pull Request。编译结果以GitHub Actions为准，组长**不对Stm32CubeIDE提供支持**。对网络相关API的更改需要四位组长review，由组长提交，更改其他API需要全组同学review，对实现的修改需要组长review，以下情况会被reject：

- 引入/删除/修改一些奇奇怪怪的文件，如提交自己的VSCode工作区配置、修改CmakeLists.txt等。
- 编译结果中存在自己可以解决的警告。项目使用clang-tidy进行代码静态检查，如果你认为某些警告不合理，请提issue。Pull Request前请检查GitHub Actions中自己的代码编译结果。
- 代码风格不符合Google的规范。使用cmake编译时，cmake会自动调用clang-format对代码进行格式化，提交前请务必使用cmake编译一次代码。
- 疯狂水提交。提交前请三思，如果情况特殊请百度如何合并提交。
- 提交他人代码。如果确实需要用到别人没有被review的代码，请不要提交上来。
- git操作不当甚至删库跑路（有前车之鉴，所以我锁了master和dev分支）。
- 分支名、变量名、提交信息等等没有实际意义。
- API没有详细的doxygen注释，或代码实现没有简要注释。
- 存在不考虑线程安全、滥用内存、在栈中大量使用内存、不限制递归层数等不良代码习惯，或者使用STL等不适合嵌入式开发的技术，或者滥用浮点计算等速度极慢的操作。
- 中断处理函数占用大于5μs的。耗时操作请放到事件循环里完成。禁止中断中printf。
- 使用sprintf等不检查返回值是否小于buffer长度造成缓冲区溢出

其中变量命名约定如下（后期可能使用clang-tidy检查，但是STM32Cube给出的代码太乱了）：

- 宏、编译器常量使用`UPPER_CASE`
- 类名、结构体、联合体、枚举量使用`PascalCase`
- 函数、成员函数使用`PascalCase`，不推荐`camelCase`
- 变量、成员变量使用`lower_case`
- 类成员变量使用后缀`_`

有一些温馨提示：

- 根据https://github.com/FreeRTOS/FreeRTOS-Kernel/issues/254#issuecomment-769904807，FreeRTOS在scheduler开始之前进入临界区会关闭中断直至scheduler启动，`HAL_Delay()`等函数将失效。
- 本项目已经重写`printf`、`sprintf`，提供了不使用malloc且thread safe&reentant的实现，并借助FreeRTOS实现的线程安全。使用前请include `utility.h`。项目已经关闭newlib的reent，使用newlib的函数后果自负。

评分会严格参考有效提交（经过review）的数量和质量，没有有效提交会记0分。

如果你有本项目代码相关的问题，或者发现别人的代码里有BUG，请提issue，不要在QQ群中询问。如果你发现未被合并的Pull Request中有BUG，可以发在Pull Request评论区。

在此提醒：STM32F103C8T6只有20K内存，未完成的空项目+库代码已经占用70%，如果你的实现需要大块内存空间，请向组长申请。

由于STM32F103性能不足，后期我们可能转到esp32上进行开发。

## 开源许可

- https://github.com/STMicroelectronics/STM32CubeF1 详见 https://github.com/STMicroelectronics/STM32CubeF1/blob/master/License.md
- https://github.com/FreeRTOS/FreeRTOS MIT License
- https://github.com/ObKo/stm32-cmake MIT License
- https://github.com/mpaland/printf MIT License

## License

GPLv3
