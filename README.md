# png-editor

一个 PNG 图片的查看兼编辑平台。目前只适用于 Windows 系统。


## 开发环境

* **系统**
  
  Windows10+

* **IDE**

  Visual Studio Code

* **工具链**

  * **MSYS2 UCRT64**
  
    从 [MSYS2 官网](https://www.msys2.org/)下载 **msys2-x86_64-*.exe** (64位)，运行安装程序选择路径 (推荐默认路径 `C:\msys64`) 进行安装。

    打开 **环境变量**，添加系统变量名 **MSYS2_HOME** (值为 ***C:\msys64***)。然后编辑系统变量 **Path**，新增 ***%MSYS2_HOME%\mingw64\bin*** 和 ***%MSYS2_HOME%\usr\bin*** 并保存。如此开发者便能在命令窗口使用 `pacman` 工具命令以及后续安装的其他命令 (如 `gcc`、`mingw32-make` 等)。

    打开安装好的 **MSYS2 UCRT64**，在窗口中输入并运行 (如果提示关闭终端，重新打开再次运行 `pacman -Su`)：

      ```bash
      pacman -Syu               # 更新核心包
      
      pacman -Su                # 完成剩余更新
      ```

    需要注意的是，这里使用的终端始终都是 **MSYS2 UCRT64**，而不是 **MSYS2 MSYS** 或 **MSYS2 MINGW64**。

  * **gcc** 与 **mingw32-make**

    通过 **MSYS2 UCRT64** 终端安装：

      ```bash
      pacman -S mingw-w64-x86_64-toolchain
      ```
    
    测试是否安装成功：

      ```bash
      gcc --version

      mingw32-make --version
      ```

  * **zlib**

    这是 PNG 依赖的动态连接库，通过 **MSYS2 UCRT64** 终端安装：

      ```bash
      pacman -S mingw-w64-x86_64-zlib
      ```

  * **gdb**

    这是程序的调试器，可与 VSCode 结合进行断点调试。通过 **MSYS2 UCRT64** 终端安装：

      ```bash
      pacman -S mingw-w64-x86_64-gdb
      ```


## 开发过程

* 拉取代码

  ```bash
  git clone git@github.com:ZhongyiChen/png-editor.git
  ```

* 编译应用

  使用管理员身份打开 **MSYS2 UCRT64** 把应用编译为 exe 文件。

  ```bash
  cd png-editor

  mingw32-make
  ```

* 调试应用

  打开 Makefile 文件，在 `CFLAGS = -Wall -Wextra -O2 -I.` 末尾添加 `-g` 参数：

    ```Makefile
    CFLAGS = -Wall -Wextra -O2 -I. -g
    ```

  重新编译应用：

    ```bash
    # 移除打包内容
    mingw32-make clean

    # 重新打包
    mingw32-make
    ```
  
  在 VSCode 中给想要调试的代码左侧标记测试点，然后在 **运行与调试** 栏启动调试即可。

* 移植应用

  本应用为绿色应用，将编译后的 **dist** 目录打包后发送到目标计算机即可。


## 功能模块

- [] 图片展示
- [] 图片缩放
