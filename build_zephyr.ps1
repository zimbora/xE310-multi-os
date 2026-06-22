#!/usr/bin/env pwsh

$ErrorActionPreference = "Stop"

Push-Location -Path "C:\ncs\v3.3.0\zephyr"
try {
    west build -b nrf54l15dk/nrf54l15/cpuapp $PSScriptRoot --pristine
}
finally {
    Pop-Location
}
