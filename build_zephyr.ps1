# build_zephyr.ps1
Push-Location C:\ncs\v2.8.0\zephyr
west build -b nrf54l15dk/nrf54l15/cpuapp $PSScriptRoot --pristine
Pop-Location