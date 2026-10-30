param(
    [string]$QtPath,
    [string]$SdkPath,
    [string]$VisualStudioPath,
    [string]$CMakePath,
    [string]$NinjaPath,
    [string]$PackagePath,
    [switch]$SkipTests,
    [switch]$SkipPackage
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build-windows'
if (!$QtPath) { $QtPath = Join-Path $build 'deps\qt\6.10.3\msvc2022_64' }
if (!$SdkPath) {
    $SdkPath = Join-Path $build 'deps\sdk'
    if (!(Test-Path "$SdkPath\Include")) { $SdkPath = "${env:ProgramFiles(x86)}\Windows Kits\10" }
}
if (!$VisualStudioPath) {
    $candidates = @($env:VSINSTALLDIR, "$env:ProgramFiles\Microsoft Visual Studio\2022\Community", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools")
    $VisualStudioPath = $candidates | Where-Object { $_ -and (Test-Path "$_\VC\Tools\MSVC") } | Select-Object -First 1
}
if (!$VisualStudioPath) { throw 'MSVC x64 tools were not found. Specify -VisualStudioPath.' }
$vc = Get-ChildItem "$VisualStudioPath\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdkVersion = Get-ChildItem "$SdkPath\Include" -Directory | Where-Object { Test-Path "$($_.FullName)\ucrt\stdio.h" } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$sdkVersion -or !(Test-Path "$QtPath\lib\cmake\Qt6\Qt6Config.cmake")) { throw 'Qt or Windows SDK is missing. Specify -QtPath and -SdkPath.' }
$sdkVersion = $sdkVersion.Name
$vcBin = Join-Path $vc.FullName 'bin\Hostx64\x64'
function Find-Tool($name, $specified, $fallbacks) {
    if ($specified) { return (Resolve-Path $specified).Path }
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $fallbacks) { if (Test-Path $candidate) { return $candidate } }
    throw "$name was not found."
}
$CMakePath = Find-Tool 'cmake.exe' $CMakePath @("$VisualStudioPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
$NinjaPath = Find-Tool 'ninja.exe' $NinjaPath @("$VisualStudioPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
$keychain = Join-Path $build 'deps\keychain'
$appBuild = Join-Path $build 'app'

function Invoke-Tool([string]$program, [string[]]$arguments) {
    Write-Host ("Running: {0} {1}" -f [IO.Path]::GetFileName($program), ($arguments -join ' '))
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $program
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # All arguments are generated here; Windows file paths cannot contain quotes.
    $start.Arguments = (($arguments | ForEach-Object {
        '"' + [regex]::Replace([regex]::Replace($_, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
    }) -join ' ')
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Host $stdout.GetAwaiter().GetResult()
    Write-Host $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "$program failed with exit code $($process.ExitCode)." }
}

$names = @('Path', 'INCLUDE', 'LIB', 'LIBPATH', 'WindowsSdkDir', 'WindowsSDKVersion', 'UniversalCRTSdkDir', 'UCRTVersion', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QT_QPA_FONTDIR', 'QML2_IMPORT_PATH')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:Path = "$vcBin;$SdkPath\bin\$sdkVersion\x64;$(Split-Path $CMakePath);$(Split-Path $NinjaPath);$QtPath\bin;$keychain\bin;$env:Path"
    $env:INCLUDE = "$($vc.FullName)\include;$SdkPath\Include\$sdkVersion\ucrt;$SdkPath\Include\$sdkVersion\shared;$SdkPath\Include\$sdkVersion\um;$SdkPath\Include\$sdkVersion\winrt"
    $env:LIB = "$($vc.FullName)\lib\x64;$SdkPath\Lib\$sdkVersion\ucrt\x64;$SdkPath\Lib\$sdkVersion\um\x64"
    $env:LIBPATH = "$($vc.FullName)\lib\x64"
    $env:WindowsSdkDir = "$SdkPath\"
    $env:WindowsSDKVersion = "$sdkVersion\"
    $env:UniversalCRTSdkDir = "$SdkPath\"
    $env:UCRTVersion = $sdkVersion
    $env:QT_PLUGIN_PATH = $null; $env:QT_QPA_PLATFORM = $null; $env:QML2_IMPORT_PATH = $null
    $env:QT_QPA_FONTDIR = "$env:SystemRoot\Fonts"
    $source = Join-Path $build 'deps\src\qtkeychain-0.14.3'
    if (!(Test-Path "$keychain\lib\cmake\Qt6Keychain\Qt6KeychainConfig.cmake")) {
        if (!(Test-Path "$source\CMakeLists.txt")) { throw 'QtKeychain 0.14.3 source is missing from build-windows/deps/src.' }
        Invoke-Tool $CMakePath @('-S', $source, '-B', "$build\deps\keychain-build", '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$NinjaPath", '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtPath", "-DCMAKE_INSTALL_PREFIX=$keychain", '-DBUILD_WITH_QT6=ON', '-DBUILD_TRANSLATIONS=OFF', '-DBUILD_TEST_APPLICATION=OFF', '-DUSE_CREDENTIAL_STORE=ON')
        Invoke-Tool $CMakePath @('--build', "$build\deps\keychain-build", '--parallel', '4')
        Invoke-Tool $CMakePath @('--install', "$build\deps\keychain-build")
    }
    Invoke-Tool $CMakePath @('-S', $root, '-B', $appBuild, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$NinjaPath", '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtPath;$keychain", '-DBUILD_TESTING=ON')
    Invoke-Tool $CMakePath @('--build', $appBuild, '--parallel', '4')
    if (!$SkipTests) { Invoke-Tool (Join-Path (Split-Path $CMakePath) 'ctest.exe') @('--test-dir', $appBuild, '--output-on-failure') }
    if (!$SkipPackage) {
        $package = if ($PackagePath) { [IO.Path]::GetFullPath($PackagePath) } else { Join-Path $root 'out\qtNorth-windows-x64' }
        New-Item -ItemType Directory -Force $package | Out-Null
        Copy-Item "$appBuild\qtNorth.exe" $package -Force
        Copy-Item "$keychain\bin\qt6keychain.dll" $package -Force
        # QtKeychain is third-party despite its Qt6-prefixed DLL name. Deploy our
        # known Qt modules from Widgets so windeployqt does not search for it in Qt/bin.
        Invoke-Tool "$QtPath\bin\windeployqt.exe" @('--release', '--no-translations', '--no-compiler-runtime', '--no-system-dxc-compiler', '--widgets', '--network', '--svg', '--dir', $package, "$QtPath\bin\Qt6Widgets.dll")
        New-Item -ItemType Directory -Force "$package\licenses" | Out-Null
        Copy-Item "$source\COPYING" "$package\licenses\QtKeychain.txt" -Force
        if (Test-Path "$QtPath\licenses") { Copy-Item "$QtPath\licenses\*" "$package\licenses" -Recurse -Force }
        Copy-Item "$root\README.md" "$package\README-source.md" -Force
        Copy-Item "$root\DISCLAIMER.txt" "$package\DISCLAIMER.txt" -Force
        Copy-Item "$root\LICENSE" "$package\LICENSE" -Force
        Copy-Item "$root\THIRD_PARTY_NOTICES.txt" "$package\THIRD_PARTY_NOTICES.txt" -Force
        Copy-Item "$root\licenses\*" "$package\licenses" -Recurse -Force
        if (Test-Path "$build\deps\vc_redist.x64.exe") {
            New-Item -ItemType Directory -Force "$package\runtime" | Out-Null
            Copy-Item "$build\deps\vc_redist.x64.exe" "$package\runtime" -Force
        }
        @'
qtNorth for Windows x64

Extract the whole folder, then open qtNorth.exe. Keep the DLLs and plugins beside it.
Built and tested on Windows 11 x64. Requires the Microsoft Visual C++ x64 Runtime.
If the runtime is missing, run runtime/vc_redist.x64.exe or download https://aka.ms/vs/17/release/vc_redist.x64.exe .
Credentials are saved in Windows Credential Manager. Logout removes saved credentials.
'@ | Set-Content "$package\README.txt" -Encoding UTF8
        Compress-Archive -Path $package -DestinationPath "$package.zip" -Force
        Write-Host "Built package: $package.zip"
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
