param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

if (-not $IdfArgs -or $IdfArgs.Count -eq 0) {
    $IdfArgs = @("build")
}

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$env:IDF_PATH = (Resolve-Path "..\tools\esp-idf").Path
$env:IDF_TOOLS_PATH = (Resolve-Path ".\.espressif").Path
$env:IDF_COMPONENT_CACHE_PATH = (Resolve-Path ".\.component_cache").Path
$env:IDF_PYTHON_ENV_PATH = (Resolve-Path ".\.idf_pyenv").Path
$env:ESP_ROM_ELF_DIR = (Resolve-Path ".\.espressif\tools\esp-rom-elfs\*").Path

$cm = (Resolve-Path ".\.espressif\tools\cmake\*\bin").Path
$ninja = (Resolve-Path ".\.espressif\tools\ninja\*").Path
$xtensa = (Resolve-Path ".\.espressif\tools\xtensa-esp-elf\*\xtensa-esp-elf\bin").Path
$riscv = (Resolve-Path ".\.espressif\tools\riscv32-esp-elf\*\riscv32-esp-elf\bin").Path
$ulp = (Resolve-Path ".\.espressif\tools\esp32ulp-elf\*\esp32ulp-elf\bin").Path
$idfexe = (Resolve-Path ".\.espressif\tools\idf-exe\*").Path
$ccache = (Resolve-Path ".\.espressif\tools\ccache\*").Path

$env:PATH = "$cm;$ninja;$xtensa;$riscv;$ulp;$idfexe;$ccache;$env:PATH"

$env:GIT_CONFIG_COUNT = "2"
$env:GIT_CONFIG_KEY_0 = "safe.directory"
$env:GIT_CONFIG_VALUE_0 = "C:/Users/NEWLIFE/Documents/Arduino/audiodecoder/UAS/sewu_audio_s3/tools/esp-idf"
$env:GIT_CONFIG_KEY_1 = "safe.directory"
$env:GIT_CONFIG_VALUE_1 = "C:/Users/NEWLIFE/Documents/Arduino/audiodecoder/UAS/sewu_audio_s3/tools/esp-idf/components/openthread/openthread"

$idfPy = ".\.idf_pyenv\Scripts\python.exe"
& $idfPy "$env:IDF_PATH\tools\idf.py" @IdfArgs
exit $LASTEXITCODE
