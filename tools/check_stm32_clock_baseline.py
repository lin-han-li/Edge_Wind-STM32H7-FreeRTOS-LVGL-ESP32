from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STM32 = ROOT / "STM32H7+FreeRTOS+LVGL+ESP32"
IOC = STM32 / "STM32H750XBH6.ioc"
MAIN_C = STM32 / "Core" / "Src" / "main.c"


EXPECTED_IOC = {
    "PC14-OSC32_IN\\ (OSC32_IN).Mode": "LSE-External-Oscillator",
    "PC15-OSC32_OUT\\ (OSC32_OUT).Mode": "LSE-External-Oscillator",
    "PH0-OSC_IN\\ (PH0).Mode": "HSE-External-Oscillator",
    "PH1-OSC_OUT\\ (PH1).Mode": "HSE-External-Oscillator",
    "RCC.HSE_VALUE": "25000000",
    "RCC.PLLSourceVirtual": "RCC_PLLSOURCE_HSE",
    "RCC.DIVM1": "5",
    "RCC.DIVN1": "192",
    "RCC.SYSCLKSource": "RCC_SYSCLKSOURCE_PLLCLK",
    "RCC.SYSCLKFreq_VALUE": "480000000",
    "RCC.HCLKFreq_Value": "240000000",
    "RCC.APB1Freq_Value": "120000000",
    "RCC.APB2Freq_Value": "120000000",
    "RCC.APB3Freq_Value": "120000000",
    "RCC.APB4Freq_Value": "120000000",
    "RCC.FMCFreq_Value": "240000000",
    "RCC.QSPIFreq_Value": "240000000",
    "RCC.SDMMCFreq_Value": "240000000",
    "RCC.LTDCFreq_Value": "50000000",
    "RCC.USART16Freq_Value": "120000000",
    "RCC.SPI123Freq_Value": "160000000",
    "RCC.RTCClockSelection": "RCC_RTCCLKSOURCE_LSE",
}

EXPECTED_MAIN_SNIPPETS = [
    "HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);",
    "__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);",
    "RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;",
    "RCC_OscInitStruct.HSEState = RCC_HSE_ON;",
    "RCC_OscInitStruct.LSEState = RCC_LSE_ON;",
    "RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;",
    "RCC_OscInitStruct.PLL.PLLM = 5;",
    "RCC_OscInitStruct.PLL.PLLN = 192;",
    "RCC_OscInitStruct.PLL.PLLP = 2;",
    "RCC_OscInitStruct.PLL.PLLQ = 4;",
    "RCC_OscInitStruct.PLL.PLLR = 2;",
    "RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;",
    "RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;",
    "RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;",
    "RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;",
    "RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;",
    "RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;",
    "RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;",
    "HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4)",
]


def load_ioc_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def main() -> int:
    errors: list[str] = []

    if not IOC.exists():
        errors.append(f"missing ioc: {IOC}")
    else:
        values = load_ioc_values(IOC)
        for key, expected in EXPECTED_IOC.items():
            actual = values.get(key)
            if actual != expected:
                errors.append(f"{IOC.name}: {key} expected {expected!r}, got {actual!r}")

    if not MAIN_C.exists():
        errors.append(f"missing main.c: {MAIN_C}")
    else:
        text = MAIN_C.read_text(encoding="utf-8", errors="replace")
        for snippet in EXPECTED_MAIN_SNIPPETS:
            if snippet not in text:
                errors.append(f"{MAIN_C.name}: missing snippet {snippet!r}")

    if errors:
        print("Clock baseline check FAILED:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("Clock baseline check OK: HSE/LSE/PLL/bus clocks match the protected baseline.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
