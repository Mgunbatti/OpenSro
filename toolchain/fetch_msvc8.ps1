# Downloads the portable Visual C++ 2005 SP1 toolchain into toolchain\msvc8sp1 (nothing is installed).
# The archive is a community redistribution pinned by SHA-256 (same pin as jaenster/setup-msvc).
# A second, independent copy is downloaded to cross-check the compiler binaries byte for byte,
# because cl's c1/c1xx/c2 DLLs carry no Authenticode signature.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # the PS 5.1 progress bar makes downloads ~100x slower
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dl   = Join-Path $here '_dl'
$dst  = Join-Path $here 'msvc8sp1'
$main = @{ url = 'https://github.com/archaic-msvc/msvc800_sp1/archive/4b1bafba636f67eb76f548f0f0e7f38864091a4a.tar.gz'
           sha = '7ddbb5064be4bac26412f1f09a837bd5b533c2f84fa03cb6ed412a573d8a89eb'; file = 'msvc800_sp1.tar.gz' }
$alt  = @{ url = 'https://github.com/Drahsid/ffxiv_msvc8.0sp1/archive/0d6f444cfc120e4e62a092726844fa310c74b1bc.tar.gz'
           sha = 'e728de13878c2876e855e2206fc4b8b0a52a3e801c29bd8e0478ef1670aa7be7'; file = 'ffxiv_msvc8sp1.tar.gz' }

# Windows' own curl/tar by full path: from Git Bash, GNU tar comes first on PATH and reads "C:" as a host
$curl = Join-Path $env:SystemRoot 'System32\curl.exe'
$tar  = Join-Path $env:SystemRoot 'System32\tar.exe'

function Get-Pinned($p) {
    $f = Join-Path $dl $p.file
    if (-not (Test-Path $f)) { & $curl -sSL -o $f $p.url; if ($LASTEXITCODE) { throw "download failed: $($p.url)" } }
    $h = (Get-FileHash $f -Algorithm SHA256).Hash.ToLower()
    if ($h -ne $p.sha) { throw "SHA-256 mismatch for $($p.file): $h" }
    return $f
}

New-Item -ItemType Directory -Force $dl, $dst | Out-Null
$mainTar = Get-Pinned $main
$altTar  = Get-Pinned $alt
& $tar -xzf $mainTar -C $dst --strip-components=1
if ($LASTEXITCODE) { throw "extracting $mainTar failed" }
$altDir = Join-Path $dl 'alt'; New-Item -ItemType Directory -Force $altDir | Out-Null
& $tar -xzf $altTar -C $altDir --strip-components=1
if ($LASTEXITCODE) { throw "extracting $altTar failed" }

# cross-check every exe/dll present in both copies
$a = @{}; Get-ChildItem $dst -Recurse -Include *.exe,*.dll | ForEach-Object { $a[$_.Name.ToLower()] += @((Get-FileHash $_.FullName).Hash) }
$bad = 0; $n = 0
Get-ChildItem $altDir -Recurse -Include *.exe,*.dll | ForEach-Object {
    $k = $_.Name.ToLower()
    if ($a.ContainsKey($k)) { $n++; if ($a[$k] -notcontains (Get-FileHash $_.FullName).Hash) { $bad++; Write-Warning "differs: $k" } }
}
if ($bad) { throw "$bad binaries differ between the two copies" }
if ($n -lt 10) { throw "only $n binaries could be cross-checked" }
cmd /c "`"$(Join-Path $dst 'VC\bin\cl.exe')`" 2>&1" | Select-Object -First 1   # cl prints its banner on stderr
Write-Host "OK: $n binaries identical in both copies; toolchain in $dst"
Remove-Item -Recurse -Force $dl
