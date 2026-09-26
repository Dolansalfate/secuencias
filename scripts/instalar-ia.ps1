# Instala los motores de IA de Secuencias en Windows, una vez, en el usuario actual:
#   %USERPROFILE%\demucs-env    Demucs (separar canciones en stems) con PyTorch
#   %USERPROFILE%\analisis-env  madmom (tempo, compases, acordes, tonalidad)
# Requiere Python 3.9 a 3.12 (python.org, con "Add python.exe to PATH"). Se puede repetir sin problema.
#
#   powershell -ExecutionPolicy Bypass -File instalar-ia.ps1              Demucs y madmom
#   powershell -ExecutionPolicy Bypass -File instalar-ia.ps1 -Roformer    además audio-separator (voces mejores; pesado, requiere ffmpeg)
param([switch]$Roformer)

$ErrorActionPreference = "Continue"
function Fallo($msg) {
    Write-Host ""
    Write-Host "ERROR: $msg" -ForegroundColor Red
    Read-Host "Pulsa Enter para cerrar" | Out-Null
    exit 1
}
function Ejecutar($exe, $argumentos) {
    & $exe @argumentos
    if ($LASTEXITCODE -ne 0) { return $false }
    return $true
}

Write-Host "== Secuencias: instalación de los motores de IA (Windows) =="
Write-Host ""

# ------------------------------------------------------------------ Python 3.9 a 3.12
$pyCmd = $null; $pyArgs = @()
if (Get-Command py -ErrorAction SilentlyContinue) {
    foreach ($v in @("3.12", "3.11", "3.10", "3.9")) {
        & py "-$v" -c "import sys" 2>$null
        if ($LASTEXITCODE -eq 0) { $pyCmd = "py"; $pyArgs = @("-$v"); break }
    }
}
if (-not $pyCmd -and (Get-Command python -ErrorAction SilentlyContinue)) {
    $ver = & python -c "import sys; print(sys.version_info[0]*100 + sys.version_info[1])" 2>$null
    if ($LASTEXITCODE -eq 0 -and [int]$ver -ge 309 -and [int]$ver -le 312) { $pyCmd = "python"; $pyArgs = @() }
}
if (-not $pyCmd) {
    Fallo "Se necesita Python 3.9 a 3.12 (PyTorch y madmom no tienen versiones para uno más nuevo). Instálalo desde https://www.python.org/downloads/windows/ marcando 'Add python.exe to PATH' y vuelve a ejecutar."
}
$pyVersion = & $pyCmd @pyArgs -c "import sys; print('%d.%d' % sys.version_info[:2])"
$pyTag = & $pyCmd @pyArgs -c "import sys; print('cp%d%d' % sys.version_info[:2])"
Write-Host "Python $pyVersion ($pyCmd $pyArgs)"

$home_ = $env:USERPROFILE
$demucsEnv = Join-Path $home_ "demucs-env"
$analisisEnv = Join-Path $home_ "analisis-env"

# ------------------------------------------------------------------ Demucs
Write-Host ""
Write-Host "== 1/2  Demucs (separación en stems) en $demucsEnv =="
if (-not (Test-Path (Join-Path $demucsEnv "Scripts\python.exe"))) {
    if (-not (Ejecutar $pyCmd ($pyArgs + @("-m", "venv", $demucsEnv)))) { Fallo "No se pudo crear $demucsEnv" }
}
$pip = Join-Path $demucsEnv "Scripts\pip.exe"
Ejecutar $pip @("install", "--upgrade", "pip", "wheel") | Out-Null

$torch = @("torch==2.8.0", "torchaudio==2.8.0", "torchvision==0.23.0")
$nvidia = $false
if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) { & nvidia-smi > $null 2>&1; if ($LASTEXITCODE -eq 0) { $nvidia = $true } }
if ($nvidia) {
    $index = "https://download.pytorch.org/whl/cu126"   # NVIDIA, incluidas tarjetas viejas
    Write-Host "Tarjeta NVIDIA detectada: PyTorch con CUDA 12.6"
} else {
    $index = "https://download.pytorch.org/whl/cpu"
    Write-Host "Sin tarjeta NVIDIA: PyTorch para CPU"
}
# PyTorch exacto desde su índice; luego Demucs desde PyPI con el índice de PyTorch como extra y los
# pines repetidos: sin ellos pip cambia PyTorch y Demucs deja de funcionar
if (-not (Ejecutar $pip (@("install") + $torch + @("--index-url", $index)))) { Fallo "No se pudo instalar PyTorch" }
if (-not (Ejecutar $pip (@("install", "demucs", "soundfile") + $torch + @("--extra-index-url", $index)))) { Fallo "No se pudo instalar Demucs" }
if ($Roformer) {
    if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { Write-Host "AVISO: Roformer necesita ffmpeg en el PATH (por ejemplo: winget install ffmpeg)" -ForegroundColor Yellow }
    if (-not (Ejecutar $pip (@("install", "audio-separator", "onnxruntime") + $torch + @("--extra-index-url", $index)))) { Fallo "No se pudo instalar audio-separator" }
}
$demucsPy = Join-Path $demucsEnv "Scripts\python.exe"
if (-not (Ejecutar $demucsPy @("-c", "import demucs, torch; print('Demucs listo, PyTorch', torch.__version__)"))) { Fallo "Demucs no importa" }

# ------------------------------------------------------------------ madmom
Write-Host ""
Write-Host "== 2/2  madmom (tempo, compases y acordes) en $analisisEnv =="
if (-not (Test-Path (Join-Path $analisisEnv "Scripts\python.exe"))) {
    if (-not (Ejecutar $pyCmd ($pyArgs + @("-m", "venv", $analisisEnv)))) { Fallo "No se pudo crear $analisisEnv" }
}
$pip2 = Join-Path $analisisEnv "Scripts\pip.exe"
$analisisPy = Join-Path $analisisEnv "Scripts\python.exe"
if (-not (Ejecutar $pip2 @("install", "--upgrade", "pip", "wheel", "numpy<2", "cython<3"))) { Fallo "No se pudo instalar numpy" }
& $analisisPy -c "import madmom" 2>$null
if ($LASTEXITCODE -ne 0) {
    # madmom no tiene versión precompilada en PyPI para Windows: el instalador de Secuencias trae ruedas
    # compiladas para Python 3.10 a 3.12 en la carpeta wheels\ (las arma GitHub Actions)
    $wheel = Get-ChildItem -Path (Join-Path $PSScriptRoot "wheels") -Filter "madmom-*$pyTag*.whl" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($wheel) {
        Write-Host "Usando madmom precompilado: $($wheel.Name)"
        if (-not (Ejecutar $pip2 @("install", $wheel.FullName))) { Fallo "No se pudo instalar madmom" }
    } else {
        Write-Host "No hay madmom precompilado para Python $pyVersion; se intenta compilar (requiere las herramientas de compilación de Visual Studio)..." -ForegroundColor Yellow
        if (-not (Ejecutar $pip2 @("install", "--no-build-isolation", "git+https://github.com/CPJKU/madmom"))) {
            Fallo "No se pudo compilar madmom. Instala Python 3.12 (o 3.11 o 3.10) y vuelve a ejecutar, o instala 'Visual Studio Build Tools' con C++."
        }
    }
}
if (-not (Ejecutar $analisisPy @("-c", "import madmom; print('madmom listo')"))) { Fallo "madmom no importa" }

Write-Host ""
Write-Host "== Todo listo. Los modelos se descargan la primera vez que separes o analices una canción. =="
Write-Host "La app busca Python en $demucsEnv y $analisisEnv (Ajustes IA permite cambiarlo)."
Write-Host ""
Read-Host "Pulsa Enter para cerrar" | Out-Null
