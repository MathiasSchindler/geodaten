param(
    [string]$OutputDirectory = 'build/freestanding-windows-arm64',
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = $PSScriptRoot
$outputPath = Join-Path $projectRoot $OutputDirectory
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$commonFlags = @(
    '-target', 'aarch64-w64-windows-gnu',
    '-std=c11', '-Os', '-ffreestanding', '-fno-builtin',
    '-fno-stack-protector', '-mno-stack-arg-probe',
    '-fdata-sections', '-ffunction-sections',
    '-fno-asynchronous-unwind-tables', '-fno-unwind-tables', '-nostdinc',
    '-Isrc/shared', '-Isrc/shared/fontrender', '-Isrc/platform/windows',
    '-Isrc/platform/common', '-DNEWOS_DISABLE_STACK_GUARD_INIT',
    '-DFR_RASTER_DISABLE_SIMD'
)
$linkFlags = @(
    '-nostdlib', '-Wl,--gc-sections', '-Wl,-e,mainCRTStartup',
    '-lkernel32', '-lws2_32'
)
$runtimeSources = @(
    'src/platform/windows/core.c',
    'src/platform/windows/thread.c',
    'src/shared/runtime/io.c',
    'src/shared/runtime/memory.c',
    'src/shared/runtime/parse.c',
    'src/shared/runtime/string.c'
)
$tools = [ordered]@{
    'pbf-info' = @(
        'src/shared/compression/zlib.c',
        'src/shared/pbf.c',
        'src/tools/pbf_info.c'
    )
    'osm-lookup' = @(
        'src/shared/compression/zlib.c',
        'src/shared/osm_index.c',
        'src/shared/pbf.c',
        'src/tools/osm_lookup.c'
    )
    'osm-addresses' = @(
        'src/shared/compression/zlib.c',
        'src/shared/pbf.c',
        'src/tools/osm_addresses.c'
    )
    'osm-buildings' = @(
        'src/shared/compression/zlib.c',
        'src/shared/osm_index.c',
        'src/shared/pbf.c',
        'src/tools/osm_buildings.c'
    )
    'osm-postal' = @(
        'src/shared/compression/zlib.c',
        'src/shared/pbf.c',
        'src/tools/osm_postal.c'
    )
    'osm-postal-fixture' = @('src/tools/osm_postal_fixture.c')
    'libre-hkde' = @('src/tools/libre_hkde.c')
    'pbf-to-rpack' = @(
        'src/shared/compression/zlib.c',
        'src/shared/pbf.c',
        'src/shared/osmrpack.c',
        'src/tools/pbf_to_rpack.c'
    )
    'rpack-info' = @(
        'src/shared/osmrpack.c',
        'src/tools/rpack_info.c'
    )
    'rpack-render' = @(
        'src/shared/compression/crc32.c',
        'src/shared/compression/zlib.c',
        'src/shared/pbf.c',
        'src/shared/osmrpack.c',
        'src/shared/simple_config.c',
        'src/tools/rpack_render.c'
    )
    'pbf-to-rte' = @(
        'src/shared/compression/zlib.c',
        'src/shared/pbf.c',
        'src/tools/pbf_to_rte.c'
    )
    'rte-info' = @('src/tools/rte_info.c')
    'rte-route' = @('src/tools/rte_route.c')
    'test-thread' = @('src/tools/test_thread.c')
    'test-font' = @(
        'src/shared/fontrender/fr_platform.c',
        'src/shared/fontrender/fr_ttf.c',
        'src/shared/fontrender/fr_raster.c',
        'src/shared/fontrender/font_backend_truetype.c',
        'src/shared/fontrender_runtime.c',
        'src/tools/test_font.c'
    )
}

Push-Location $projectRoot
try {
    foreach ($toolName in $tools.Keys) {
        $outputFile = Join-Path $outputPath ($toolName + '.exe')
        & clang @commonFlags @runtimeSources @($tools[$toolName]) @linkFlags -o $outputFile
        if ($LASTEXITCODE -ne 0) {
            throw ('clang failed while building {0}: exit code {1}' -f $toolName, $LASTEXITCODE)
        }
    }

    if ($Test) {
        $testPath = Join-Path $outputPath 'osm-postal-test'
        if (Test-Path $testPath) {
            Remove-Item -Recurse -Force $testPath
        }
        New-Item -ItemType Directory -Path $testPath | Out-Null

        $fixtureTool = Join-Path $outputPath 'osm-postal-fixture.exe'
        $postalTool = Join-Path $outputPath 'osm-postal.exe'
        $fixture = Join-Path $testPath 'postal_fixture.osm.pbf'
        $geojson = Join-Path $testPath 'postal.geojson'
        $repeatGeojson = Join-Path $testPath 'postal_repeat.geojson'
        $report = Join-Path $testPath 'postal_report.tsv'

        & $fixtureTool $fixture
        if ($LASTEXITCODE -ne 0) { throw 'osm-postal-fixture failed' }
        $statistics = & $postalTool $fixture $geojson --report $report --quiet
        if ($LASTEXITCODE -ne 0) { throw 'osm-postal failed' }
        & $postalTool $fixture $repeatGeojson --quiet | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'osm-postal repeat failed' }

        $textComparisons = @(
            @($geojson, 'tests/expected/postal_fixture.geojson'),
            @($report, 'tests/expected/postal_fixture_report.tsv')
        )
        foreach ($comparison in $textComparisons) {
            $actualText = [IO.File]::ReadAllText($comparison[0]).Replace("`r`n", "`n")
            $expectedText = [IO.File]::ReadAllText(
                (Join-Path $projectRoot $comparison[1])
            ).Replace("`r`n", "`n")
            if ($actualText -ne $expectedText) {
                throw ('output differs from expected file: {0}' -f $comparison[0])
            }
        }
        $firstHash = (Get-FileHash -Algorithm SHA256 $geojson).Hash
        $repeatHash = (Get-FileHash -Algorithm SHA256 $repeatGeojson).Hash
        if ($firstHash -ne $repeatHash) {
            throw 'two osm-postal runs produced different output'
        }

        $expectedStatistics = [IO.File]::ReadAllText(
            (Join-Path $projectRoot 'tests/expected/postal_fixture_stats.txt')
        ).Replace("`r`n", "`n").TrimEnd()
        $actualStatistics = ($statistics -join "`n").Replace("`r`n", "`n").TrimEnd()
        if ($actualStatistics -ne $expectedStatistics) {
            throw 'osm-postal statistics differ from expected output'
        }

        & $postalTool $fixture (Join-Path $testPath 'strict.geojson') --strict --quiet | Out-Null
        if ($LASTEXITCODE -eq 0) { throw 'osm-postal --strict unexpectedly succeeded' }
        & $postalTool $fixture (Join-Path $testPath 'strict-incomplete.geojson') --strict-incomplete --quiet | Out-Null
        if ($LASTEXITCODE -eq 0) { throw 'osm-postal --strict-incomplete unexpectedly succeeded' }

        $threadOutput = & (Join-Path $outputPath 'test-thread.exe')
        if ($LASTEXITCODE -ne 0 -or ($threadOutput -join "`n") -notmatch 'status: ok') {
            throw 'Windows ARM64 thread test failed'
        }

        foreach ($executable in Get-ChildItem -Path $outputPath -Filter '*.exe') {
            $metadata = & llvm-readobj --file-headers --coff-imports $executable.FullName
            if ($LASTEXITCODE -ne 0) {
                throw ('llvm-readobj failed for {0}' -f $executable.Name)
            }
            $metadataText = $metadata -join "`n"
            if ($metadataText -notmatch 'Machine: IMAGE_FILE_MACHINE_ARM64') {
                throw ('not an ARM64 PE executable: {0}' -f $executable.Name)
            }
            if ($metadataText -match '(?i)(ucrt|msvcr|api-ms-win-crt)') {
                throw ('C runtime import found in {0}' -f $executable.Name)
            }
        }

        Write-Output 'Windows ARM64 build, binary checks, and regression tests passed.'
    }
} finally {
    Pop-Location
}