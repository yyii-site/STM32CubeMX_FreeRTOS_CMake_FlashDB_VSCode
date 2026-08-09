# STM32CubeMX + FreeRTOS + CMake + FlashDB 项目移植与开发指南

本项目基于 **STM32CubeMX** + **CMake** + **FreeRTOS** 构建，使用 **Git Submodule** 引入第三方开源存储组件 **SFUD**、**FAL** 与 **FlashDB**，实现了在外置 SPI Flash（W25Q32，4MB）上高效管理 KV 键值对与 TSDB 时序数据库。

工程需要 STM32CubeMX 打开 stm32f401_flashdb.ioc 生成。所以项目仅会上传必要文件。

测试开发板资料：https://github.com/weactstudio/weactstudio.ministm32f4x1

淘宝购买开发板 + 补焊 25Q32JVSIQ

项目架构和代码内容以实际为准，说明文档有时候会忘记更新。

---

## 目录结构规划

项目遵循“第三方库代码零修改、适配接口独立外置”的原则，整体目录设计如下：

```text
.
├── CMakeLists.txt                      # 主 CMake 构建配置文件
├── cmake/stm32cubemx/                  # CubeMX 自动生成的 CMake 配置
├── Core/                               # 应用逻辑与中断处理代码
├── Drivers/                            # STM32 HAL 库及 CMSIS
├── Middlewares/Third_Party/
│   ├── SFUD/                           # [Git Submodule] SFUD 串行 Flash 驱动库
│   ├── FAL/                            # [Git Submodule] FAL Flash 抽象层
│   ├── FlashDB/                        # [Git Submodule] FlashDB 数据库
│   ├── ports/                          # 本地硬件适配与接口配置层 (手动维护)
│   │   ├── sfud_cfg.h                  # SFUD 参数配置文件
│   │   ├── sfud_port.c                 # SFUD 与 STM32 SPI HAL 库对接
│   │   ├── fal_cfg.h                   # FAL 分区表与设备注册
│   │   ├── fal_flash_sfud_port.c       # FAL 与 SFUD 接口桥接
│   │   ├── fdb_cfg.h                   # FlashDB 参数配置 (使能 KVDB/TSDB)
│   │   ├── rtconfig.h                  # 目的：满足 FAL 源码中 `#include <rtconfig.h>` 的编译依赖
│   │   ├── app_flashdb.h               # FlashDB 应用逻辑与测试任务
│   │   └── app_flashdb.c               # FlashDB 应用逻辑与测试任务(包含 FreeRTOS 锁与时间适配)
│   └── CMakeLists.txt                  # 第三方库与 ports 统一构建脚本
└── .vscode/
    ├── settings.json                   # 绑定交叉编译工具链（使用stm32cubemx自带环境）
    └── launch.json                     # VS Code + Cortex-Debug + OpenOCD 调试配置


```

---

## 1. STM32CubeMX 工程创建

1. **芯片/时钟配置**：配置 HSE 系统时钟及外设（例如以 STM32F401 为例，主频设为 84MHz）。
2. **外设配置**：
* **SPI1**：配置为 Master 模式，用于连接 W25Q32（根据硬件分配 CS 引脚为 GPIO 输出）。
* **USART1**：配置为 Async 异步模式（115200 8N1），用于打印系统调试日志。


3. **Middleware 配置**：
* 启用 **FreeRTOS**（CMSIS_V2），分配调度任务。flashdbTask 开发/调试阶段（开启 Debug 日志）推荐栈设置为 1024 * 4 因为 vsnprintf 极度吃栈。正式发布阶段（关闭所有 Debug 日志）可以缩减至 512 * 4 (2048 字节)。


4. **Project Manager 设置**：
* **Toolchain / IDE** 选择：**CMake**。
* 勾选 *Generate peripheral initialization as a pair of '.c/.h' files per peripheral*。



---

## 2. 引入 Git Submodules

在项目根目录下运行以下命令，将第三方库原生且干净地引入到 `Middlewares/Third_Party/` 目录下：

```bash
# 1. 引入 SFUD (串行 Flash 驱动库)
git submodule add https://github.com/armink/SFUD.git Middlewares/Third_Party/SFUD

# 2. 引入 FAL (Flash 抽象层 - 注意仓库 URL)
git submodule add https://github.com/RT-Thread-packages/fal.git Middlewares/Third_Party/FAL

# 3. 引入 FlashDB (数据库)
git submodule add https://github.com/armink/FlashDB.git Middlewares/Third_Party/FlashDB

```

---

## 3. 编写适配层 (`Middlewares/Third_Party/ports/`)

### 3.1 创建 `rtconfig.h` (Stub 桩文件)

为避免修改 FAL 源码中的 `#include <rtconfig.h>`，在 `ports/` 下建立空的 `rtconfig.h`：

```c
/* Middlewares/Third_Party/ports/rtconfig.h */
#ifndef _RTCONFIG_H_
#define _RTCONFIG_H_
/* 仅用于在 FreeRTOS 环境下满足 FAL 的编译包含需求 */
#endif

```

### 3.2 配置 SFUD (`sfud_cfg.h` & `sfud_port.c`)

* 在 `sfud_cfg.h` 中定义存储芯片型号

* 在 `sfud_port.c` 中实现 `sfud_spi_port_init()`，使用 `HAL_SPI_Transmit` / `HAL_SPI_Receive` 完成底层 SPI 收发及 CS 片选控制。这部分内容在task中运行，比 stm32cubemx 生成spi初始化的接口晚，所以自动生成的 MX_SPI1_Init 会被覆盖。

### 3.3 配置 FAL (`fal_cfg.h` & `fal_flash_sfud_port.c`)

* 在 `fal_cfg.h` 中划分 W25Q32 的 4MB 空间（例如： 1MB 分给 `kvdb`， 1MB 分给 `tsdb`）。
* 在 `fal_flash_sfud_port.c` 中将 `init` 接口重定向调用 `sfud_init()` （对比官方例成需要增加此段代码）。

### 3.4 配置 FlashDB (`fdb_cfg.h` & `app_flashdb.c/h`)

* 在 `app_flashdb.c` 中使用 FreeRTOS 互斥锁（`xSemaphoreCreateMutex`）实现 `lock` 与 `unlock`，确保多任务并发安全。

---

## 4. CMake 构建系统配置

### 4.1 第三方库 `CMakeLists.txt`

新建 `Middlewares/Third_Party/CMakeLists.txt`：

```cmake
add_library(flashdb_stack STATIC)

# 1. 包含路径 (根据 fal 仓库实际结构更新)
target_include_directories(flashdb_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ports         # 必须放在首位，覆盖子模块默认配置
    ${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/inc
)

# 2. 源文件收集
target_sources(flashdb_stack PRIVATE
    # SFUD
    ${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/src/sfud.c
    ${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/src/sfud_sfdp.c
    
    # FAL
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/src/fal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/src/fal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fal/src/fal_partition.c
    
    # FlashDB
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_kvdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_tsdb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_utils.c
    
    # 本地 ports 适配文件
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/sfud_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/fal_flash_sfud_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ports/app_flashdb.c
)

# 3. 直接链接 CubeMX 生成的 stm32cubemx 目标
# 这样 flashdb_stack 会自动继承 STM32 HAL 库的头文件路径（如 stm32f4xx_hal.h）和芯片宏定义
target_link_libraries(flashdb_stack PRIVATE
    stm32cubemx
)

```

### 4.2 根目录 `CMakeLists.txt` 修改

在工程主 `CMakeLists.txt` 中引入子目录并挂载静态库：

```cmake
# 引入 CubeMX 生成的子目录
add_subdirectory(cmake/stm32cubemx)

# 引入第三方库子目录 (必须放在 stm32cubemx 之后)
add_subdirectory(Middlewares/Third_Party)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    flashdb_stack
)

```

### 4.2 Core/Src/main.c 增加调用代码

```c
void StartFlashdbTask(void *argument)
{
  /* USER CODE BEGIN StartFlashdbTask */
  app_flashdb_init();
  app_flashdb_run_samples();
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartFlashdbTask */
}

```

---

## 5. 调试与编译参数 (VS Code)

### 5.1 串口打印配置 (`_write` 重定向)

在 `Core/Src/main.c` 中添加以下重定向，将 `printf` 输出到串口：

```c
#include <stdio.h>

int _write(int file, char *ptr, int len) {
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

```

### 5.2 Cortex-Debug 配置 (`.vscode/launch.json`)

使用 **OpenOCD** 搭配 **ST-Link**（或 CMSIS-DAP）进行在线调试：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "cwd": "${workspaceFolder}",
            "executable": "${command:cmake.launchTargetPath}",
            "name": "Debug with ST-Link (OpenOCD)",
            "request": "launch",
            "type": "cortex-debug",
            "servertype": "openocd",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32f4x.cfg"
            ],
            "armToolchainPath": "${env:HOME}/.local/share/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin",
            "runToEntryPoint": "main",
            "showDevDebugOutput": "none"
        }
    ]
}

```

记得修改路径和下载器型号

### 5.3 绑定交叉工具链 (`.vscode/settings.json`)

```json
{
    "cmake.generator": "Ninja",
    "cmake.configureEnvironment": {
        "PATH": "${env:HOME}/.local/share/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin:${env:PATH}"
    },
    "cortex-debug.variableUseNaturalFormat": true
}


```

---

## 6. 避坑指南与注意事项

1. **FreeRTOS 任务栈分配**：
* 包含 FlashDB + SFUD 调试打印（`vsnprintf`）的任务，栈空间需求较大。
* **开发/调试阶段**：建议该任务栈大小设为 **`1024 * 4` 字节 (4KB)**，避免因为栈溢出导致 `vsnprintf` 解析格式化字符（如 `%06lX`）时触发 HardFault。
* **正式发布阶段**：关闭调试日志后可缩减至 `512 * 4` 字节 (2KB)。


2. **头文件搜索顺序**：
* 在 `Middlewares/Third_Party/CMakeLists.txt` 的 `target_include_directories` 中，必须将 `${CMAKE_CURRENT_SOURCE_DIR}/ports` **放在首位**，才能确保工程加载的是自定义的 `sfud_cfg.h`，而不是 SFUD 子模块默认的配置。

## 7. 调试信息重定向

在 STM32 开发中，FlashDB、SFUD 以及系统中的 `printf` 打印信息，最常用且最稳定的读取方式是**将 `printf` 重定向到 STM32 的串口（UART），然后通过电脑上的串口助手或 VS Code 插件查看**。

下面为您介绍如何通过 3 个步骤实现串口打印与信息读取：

---

### 7.1 代码层：重定向 `printf` 到 HAL 库串口

GCC 编译器（CMake 模式）下，`printf` 最终会调用系统底层函数 `_write`。只需在工程的 `Core/Src/main.c`（或 `syscalls.c`）中重写 `_write` 函数即可：

```c
/* 在 main.c 的 /* USER CODE BEGIN PFP */ 区域添加 */
#include <stdio.h>

extern UART_HandleTypeDef huart1; // 假设使用 USART1（根据 CubeMX 配置的串口修改）

/**
  * @brief 重定向 C 标准库 printf 到串口
  */
int _write(int file, char *ptr, int len) {
    // 使用 HAL 库阻塞式发送串口数据
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

```

并在 `main()` 函数初始化阶段关闭输出缓冲区，保证日志能即时打印，不被缓存滞后：

```c
int main(void) {
    /* HAL_Init, SystemClock_Config, MX_GPIO_Init 等生成代码... */
    MX_USART1_UART_Init();

    /* 关闭 stdout 缓冲区，实现无延迟即时打印 */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 测试打印 */
    printf("\r\n--- STM32 FlashDB System Starting ---\r\n");

    /* ... 启动 FreeRTOS 调度器 ... */
}

```

---

### 7.2 硬件层：硬件连接与 CubeMX 串口配置

1. **CubeMX 配置**：
* 打开 `STM32F401.ioc`，找到 **USART1**（或 ST-Link 虚拟串口引脚 PA2/PA3）。
* Mode 选择 **Asynchronous**（异步模式）。
* Parameter Settings 设置为：**115200 Baud Rate**，**8 Bits**，**None Parity**，**1 Stop Bit**。


2. **物理引脚接线**（如果使用的是普通的 USB-TTL 串口小板）：
* STM32 `TX`（如 PA9）$\rightarrow$ USB-TTL `RX`
* STM32 `RX`（如 PA10）$\rightarrow$ USB-TTL `TX`
* STM32 `GND` $\rightarrow$ USB-TTL `GND`
*(注：如果使用的是 NUCLEO / Discovery 板载的 ST-Link，通常内部集成了 Virtual COM Port，直接用 USB 线连电脑即可，无需外接 USB-TTL)*。



---

### 7.3 工具层：在电脑上接收和查看打印日志

在电脑端读取日志有两种常用方案：

#### 方案 A：直接在 VS Code 内查看（推荐 🌟）

1. 在 VS Code 插件市场搜索并安装微软官方插件 **Serial Monitor**。
2. 安装后，点击 VS Code 底部状态栏的 **Serial Monitor** 图标。
3. 参数选择：
* **Port**：选择你的 USB 串口号（如 `COM3` 或 `/dev/ttyUSB0`）。
* **Baud Rate**：选择 `115200`。


4. 点击 **Start Monitoring**，即可在 VS Code 底部实时看到 FlashDB / SFUD 输出的日志信息。

#### 方案 B：使用独立串口调试工具

* **Windows**：推荐使用 **VOFA+**（界面极佳、支持画波形）、**SSCOM** 或 **MobaXterm**。
* **Linux / macOS**：推荐使用 **cutecom** 或终端命令 `picocom -b 115200 /dev/ttyUSB0`。

### 7.4 串口信息

```
[FlashDB][kv][env][fdb_kvdb1] (/home/yi/Code/stm32/stm32f401_flashdb/Middlewares/Third_Party/FlashDB/src/fdb_kvdb.c:1573) Sector header info is incorrect. Auto format this sector (0x000FF000).
[FlashDB][kv][env][fdb_kvdb1] All sector header is incorrect. Set it to default.
[FlashDB] FlashDB V2.2.99 is initialize success.
[FlashDB] You can get the latest version on https://github.com/armink/FlashDB .
[FlashDB][tsl][log][fdb_tsdb1] Sector (0x00000000) header info is incorrect.
[FlashDB][tsl][log][fdb_tsdb1] All sector format finished.
[FlashDB][tsl][log][fdb_tsdb1] (/home/yi/Code/stm32/stm32f401_flashdb/Middlewares/Third_Party/FlashDB/src/fdb_tsdb.c:1074) TSDB (log) oldest sectors is 0x00000000, current using sector is 0x00000000.
========================================
FlashDB 自检测试: 当前开机计数 = 1
========================================
```