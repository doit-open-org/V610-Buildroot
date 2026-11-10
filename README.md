# V610-Buildroot

面向 Cove-610 开发板的 Linux 镜像工程，集成 Hi3516CV610 厂商内核、U-Boot、MPP、无线模块及板级驱动，生成 SPI NAND 烧录文件。

## 编译镜像

需要 Linux x86_64 开发机（或 WSL2），构建路径中不能包含空格。首次构建会从 [HiSpark/Hi3516CV610](https://gitcode.com/HiSpark/Hi3516CV610) 下载厂商源码和编译器，约 2.5 GB，需保持网络畅通。

```sh
bash install.sh
make cove_610_defconfig
make
```

`make` 会自动下载 Buildroot、厂商源码和编译器。编译镜像不依赖 V610-App-SDK 和 V610-Apps。

| 项目 | 版本 |
| --- | --- |
| Buildroot | 2026.02.3 |
| 厂商 SDK | Hi3516CV610_tag_V2.1.0_Beta |
| Linux | 5.10.221 |
| 编译器 | GCC 10.3 / musl，ARM softfp |

下载缓存位于 `dl/`，厂商输入位于 `vendor/`。可提前运行 `bash scripts/fetch-vendor.sh` 准备厂商输入。离线构建需要备齐这两个目录中的所需文件。

使用其他输出目录时，后续命令保持相同的 `O`：

```sh
make O=output-dev cove_610_defconfig
make O=output-dev
```

## 镜像产物

烧录文件位于 `output/images/flash/`：

| 文件 | 内容 |
| --- | --- |
| `boot_image.bin` | GSL + U-Boot |
| `nand_env.bin` / `nand_env.txt` | U-Boot 环境及文本 |
| `uImage` | 内核与设备树 FIT 镜像 |
| `rootfs_hi3516cv610_2k_128k_59M.ubifs` | 根文件系统 UBI 镜像 |
| `nand_burn_table.xml` | NAND 烧录表 |

默认 NAND 页大小为 2 KiB、块大小为 128 KiB；分区为 boot 512 KiB、env 512 KiB、kernel 4 MiB、rootfs 59 MiB。使用 `burntool-cli` 加载 `nand_burn_table.xml` 烧录，方法见 [V610-Solution](https://github.com/doit-open-org/V610-Solution/blob/main/docs/burntool.md)。

修改分区时，同步调整 `board/cove_610/flash.cfg`、defconfig 中的 UBIFS 容量和启动配置。

## 生成应用 SDK（可选）

应用 SDK 需要与系统镜像配套。修改了系统镜像后，可以从源码生成匹配的应用 SDK。先将 V610-App-SDK 克隆到本工程根目录，再执行：

```sh
git clone https://github.com/doit-open-org/V610-App-SDK.git
make app-sdk
```

该目标构建镜像，从构建结果导出匹配的头文件和库，再生成：

```text
V610-App-SDK/dist/V610-App-SDK-<版本>.tar.gz
```

版本号由 `V610-App-SDK/VERSION` 配置。发布包不含编译器和模型转换工具 CANN，解压后通过包内的 `install.sh` 安装；应用开发方法见 SDK 仓库的 README。

这里的 platform 是导出的开发文件，无需另行编译。只更新开发文件、不打包时，在镜像构建完成后执行：

```sh
V610-App-SDK/scripts/install-platform.sh output/host
```

之后可以直接在本工程内开发应用，编译器使用镜像构建自带的那一份，不需要执行 SDK 的 `install.sh`：

```sh
source V610-App-SDK/export.sh
```

使用自定义输出目录时，执行 `make O=output-dev app-sdk`，或将导入路径改为 `output-dev/host`。板端系统库更新后，应重新生成匹配的应用 SDK。

`make sdk` 是 Buildroot 自带的完整 SDK 导出目标，与 `make app-sdk` 不同。

## 常用开发命令

```sh
make menuconfig
make savedefconfig
make linux-menuconfig
make linux-rebuild
make hisi-osdrv-rebuild
make
```

软件包重新编译后，执行 `make` 更新镜像。配置修改需通过对应保存目标写回。

| 路径 | 用途 |
| --- | --- |
| `configs/cove_610_defconfig` | Buildroot 默认配置 |
| `board/cove_610/linux.config` | 内核配置 |
| `board/cove_610/dts/` | 设备树 |
| `board/cove_610/patches/` | 内核、启动程序及 MPP 补丁 |
| `board/cove_610/rootfs_overlay/` | 根文件系统与启动脚本 |
| `package/` | 厂商及板级软件包 |
| `board/cove_610/post-build.sh` | 根文件系统后处理 |
| `board/cove_610/post-image.sh` | 烧录文件组装 |

## 板端连接

串口：`ttyAMA0`，115200。默认主机名 `cove-610`，root 密码 `123456`，可在 defconfig 中修改。

USB 网络地址：`192.168.137.2/24`，网关 `192.168.137.1`。主机配置同网段地址后可通过 SSH 登录。

`S20mpp` 加载媒体驱动，`S81sdk_server` 启动厂商服务。应用自启动由 `S40app` 的 `APP_PATH` 配置，日志写入 `/tmp/<应用名>.log`。

## LCD 与 SD 卡

LCD 与 SD 卡共用 SDIO1 引脚，同一时间只能使用其中一种，也可以都不用。`uImage` 中包含三套设备树，U-Boot 按环境变量 `sdio1_pins` 选择启动哪一套，默认为 `lcd`：

| 模式 | 设备树 | 外设 |
| --- | --- | --- |
| `lcd` | `hi3516cv610-demb-flash.dts` | SPI LCD 与触摸 |
| `sd` | `hi3516cv610-demb-flash-sd.dts` | SD 卡，同时释放 UART1 与 LSADC |
| `none` | `hi3516cv610-demb-flash-none.dts` | 无 LCD 与 SD 卡，SDIO1 关闭，同时释放 UART1 与 LSADC |

在板端切换，重启后生效：

```sh
sdio1-pins sd        # 切换到 SD 卡
sdio1-pins lcd       # 切换到 LCD
sdio1-pins none      # 不使用 LCD 和 SD 卡
sdio1-pins           # 查看当前模式和下次启动的模式
```

也可在 U-Boot 命令行执行 `setenv sdio1_pins sd; saveenv`。SD 和 none 模式下 `S30lcd`、`S31touch` 不加载驱动。

旧版本镜像中命令为 `board-periph`，U-Boot 环境变量为 `board_periph`，用法相同，但没有 `none` 模式。从旧版本升级时要同时烧录新的 `nand_env.bin`，否则 U-Boot 仍按 `board_periph` 启动，`sdio1-pins` 的切换不生效。

## 许可证

本仓库采用 [Apache License 2.0](LICENSE) 许可。`package/` 下的内核驱动和 `board/` 下的设备树按文件头声明采用 GPL-2.0；构建时下载的 Buildroot、Linux 内核和海思 SDK 遵循各自的许可证。
