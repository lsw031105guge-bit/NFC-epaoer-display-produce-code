@echo off
REM
REM XIAO nRF54LM20A 一键烧录脚本 (Windows)
REM
REM 用法:
REM   flash.bat                                REM 烧录默认的黑白(1-bit)固件
REM   flash.bat xiao_nrf54lm20a_epaper_nfc_4color.hex   REM 烧录 4 色固件
REM
REM 前置:
REM   1. 用 USB-C 线连接 XIAO nRF54LM20A(板载 SAMD11 枚举为 CMSIS-DAP)
REM   2. 已安装 openocd(建议用 PlatformIO 的 Seeed platform 自带版本)
REM      安装见 https://platformio.org/,然后:
REM        pio pkg install -g -p "https://github.com/Seeed-Studio/platform-seeedboards.git"
REM      并把 openocd 所在目录加入 PATH。

setlocal enabledelayedexpansion

set "DIR=%~dp0"
set "HEX=%~1"
if "%HEX%"=="" set "HEX=xiao_nrf54lm20a_epaper_nfc_bw.hex"
set "HEX_PATH=%DIR%%HEX%"

if not exist "%HEX_PATH%" (
    echo 错误: 找不到固件文件 %HEX_PATH%
    echo 可用固件:
    dir /b "%DIR%*.hex"
    exit /b 1
)

where openocd >nul 2>nul
if errorlevel 1 (
    echo 错误: 未找到 openocd。请先安装(见脚本头部的说明。
    exit /b 1
)

echo ^>^> 烧录: %HEX%
echo ^>^> 配置: %DIR%openocd.cfg
openocd -f "%DIR%openocd.cfg" -c "init" -c "nrf54lm20a-load %HEX_PATH%" -c "reset" -c "exit"
echo ^>^> 完成
endlocal
