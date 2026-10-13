# Third-party notices

The MIT license in `LICENSE` covers the application code, documentation, and lab tools written for this project (`Core` user logic, `docs/`, `tools/`).

The tree also vendors components that keep their own licenses:

| Component | Location | License |
|---|---|---|
| STM32CubeF4 HAL | `Drivers/STM32F4xx_HAL_Driver` | BSD-3-Clause, see `Drivers/STM32F4xx_HAL_Driver/LICENSE.txt` |
| CMSIS | `Drivers/CMSIS` | Apache-2.0 / BSD-3-Clause, see `Drivers/CMSIS/LICENSE.txt` |
| CMSIS device support | `Drivers/CMSIS/Device/ST/STM32F4xx` | BSD-3-Clause, see that folder's `LICENSE.txt` |
| FreeRTOS kernel V10.3.1 | `Middlewares/Third_Party/FreeRTOS` | MIT, see the header in `Core/Inc/FreeRTOSConfig.h` |
| FatFs | `Middlewares/Third_Party/FatFs` | FatFs license (BSD-style), see `Middlewares/Third_Party/FatFs/source/ff.c` |

CubeMX-generated files under `Core/` carry the STMicroelectronics header. User sections inside those files are part of this project.

This repository is an independent educational implementation. It is not affiliated with, endorsed by, or connected to STMicroelectronics, Sony, NXP, or any other trademark holder. Frame constants are synthetic test values shared with [BasebandManchesterDecoder](https://github.com/va-sar/BasebandManchesterDecoder).
