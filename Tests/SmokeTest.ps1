[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [string] $D3D12Executable,

    [string] $VulkanExecutable,

    [ValidateRange(5, 120)]
    [int] $TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Resolve-SampleExecutable {
    param(
        [AllowEmptyString()]
        [string] $ExplicitPath,

        [Parameter(Mandatory)]
        [string] $ProjectName
    )

    $candidate = $ExplicitPath
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        $candidate = Join-Path $repositoryRoot "Bin/$ProjectName/x64/$Configuration/$ProjectName.exe"
    }
    $resolved = Resolve-Path -LiteralPath $candidate -ErrorAction Stop
    return $resolved.Path
}

function ConvertTo-JsonLine {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Depth 30 -Compress)
}

function Read-JsonResponse {
    param(
        [Parameter(Mandatory)]
        [System.Diagnostics.Process] $Process,

        [Parameter(Mandatory)]
        [int] $ExpectedId,

        [Parameter(Mandatory)]
        [string] $Operation
    )

    $readTask = $Process.StandardOutput.ReadLineAsync()
    if (-not $readTask.Wait([TimeSpan]::FromSeconds($TimeoutSeconds))) {
        throw "$Operation timed out waiting for stdout."
    }
    $line = $readTask.GetAwaiter().GetResult()
    if ($null -eq $line) {
        throw "$Operation reached stdout EOF before a response."
    }
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "$Operation produced an empty stdout line."
    }

    try {
        $response = $line | ConvertFrom-Json
    }
    catch {
        throw "$Operation produced a non-JSON stdout line: $line"
    }
    if ($response.jsonrpc -ne '2.0' -or $response.id -ne $ExpectedId) {
        throw "$Operation returned an unexpected JSON-RPC envelope: $line"
    }
    return $response
}

function Send-JsonMessage {
    param(
        [Parameter(Mandatory)]
        [System.Diagnostics.Process] $Process,

        [Parameter(Mandatory)]
        $Message
    )

    $Process.StandardInput.WriteLine((ConvertTo-JsonLine $Message))
    $Process.StandardInput.Flush()
}

function Assert-NoUnexpectedStdout {
    param([Parameter(Mandatory)] [AllowEmptyString()] [string] $Text)

    if ([string]::IsNullOrEmpty($Text)) {
        return
    }

    $reader = [System.IO.StringReader]::new($Text)
    while ($true) {
        $line = $reader.ReadLine()
        if ($null -eq $line) {
            break
        }
        if ([string]::IsNullOrWhiteSpace($line)) {
            throw 'stdout contained an unexpected empty line.'
        }
        try {
            $null = $line | ConvertFrom-Json
        }
        catch {
            throw "stdout contained a non-JSON line: $line"
        }
        throw "stdout contained an unexpected extra JSON response: $line"
    }
}

function Invoke-McpSmokeTest {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string] $Executable
    )

    Write-Host "Testing $Name`: $Executable"

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = '--mcp'
    $startInfo.WorkingDirectory = [System.IO.Path]::GetTempPath()
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    if ($null -ne $startInfo.PSObject.Properties['StandardInputEncoding']) {
        $startInfo.StandardInputEncoding = $utf8
    }
    if ($null -ne $startInfo.PSObject.Properties['StandardOutputEncoding']) {
        $startInfo.StandardOutputEncoding = $utf8
    }
    if ($null -ne $startInfo.PSObject.Properties['StandardErrorEncoding']) {
        $startInfo.StandardErrorEncoding = $utf8
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $inputClosed = $false
    $started = $false

    try {
        if (-not $process.Start()) {
            throw "Failed to start $Executable."
        }
        $started = $true

        Send-JsonMessage $process ([ordered]@{
            jsonrpc = '2.0'
            id = 1
            method = 'initialize'
            params = [ordered]@{
                protocolVersion = '2025-11-25'
                capabilities = @{}
                clientInfo = [ordered]@{ name = 'SimpleMCPGraphicsSample smoke test'; version = '1.0' }
            }
        })
        $response = Read-JsonResponse $process 1 'initialize'
        if ($response.result.protocolVersion -ne '2025-11-25') {
            throw 'initialize returned an unexpected protocol version.'
        }

        Send-JsonMessage $process ([ordered]@{
            jsonrpc = '2.0'
            method = 'notifications/initialized'
            params = @{}
        })

        Send-JsonMessage $process ([ordered]@{ jsonrpc = '2.0'; id = 2; method = 'tools/list'; params = @{} })
        $response = Read-JsonResponse $process 2 'tools/list'
        $toolNames = @($response.result.tools | ForEach-Object { $_.name })
        foreach ($requiredTool in @('get_scene_state', 'set_camera', 'set_light', 'set_transform', 'reset_scene')) {
            if ($requiredTool -notin $toolNames) {
                throw "tools/list did not include $requiredTool."
            }
        }

        Send-JsonMessage $process ([ordered]@{ jsonrpc = '2.0'; id = 3; method = 'resources/list'; params = @{} })
        $response = Read-JsonResponse $process 3 'resources/list'
        $resourceUris = @($response.result.resources | ForEach-Object { $_.uri })
        foreach ($requiredResource in @('graphics://scene/state', 'graphics://app/info')) {
            if ($requiredResource -notin $resourceUris) {
                throw "resources/list did not include $requiredResource."
            }
        }

        Send-JsonMessage $process ([ordered]@{
            jsonrpc = '2.0'
            id = 4
            method = 'tools/call'
            params = [ordered]@{ name = 'set_camera'; arguments = [ordered]@{ fov_degrees = 52.0 } }
        })
        $response = Read-JsonResponse $process 4 'set_camera'
        if ($response.result.isError -or $response.result.structuredContent.camera.fov_degrees -ne 52.0) {
            throw 'set_camera did not update the FOV to 52 degrees.'
        }

        Send-JsonMessage $process ([ordered]@{
            jsonrpc = '2.0'
            id = 5
            method = 'tools/call'
            params = [ordered]@{
                name = 'set_transform'
                arguments = [ordered]@{ rotation_degrees = [ordered]@{ x = 0.0; y = 35.0; z = 0.0 } }
            }
        })
        $response = Read-JsonResponse $process 5 'set_transform'
        if ($response.result.isError -or $response.result.structuredContent.transform.rotation_degrees.y -ne 35.0) {
            throw 'set_transform did not update the model Y rotation to 35 degrees.'
        }

        Send-JsonMessage $process ([ordered]@{
            jsonrpc = '2.0'
            id = 6
            method = 'tools/call'
            params = [ordered]@{ name = 'get_scene_state'; arguments = @{} }
        })
        $response = Read-JsonResponse $process 6 'get_scene_state'
        if ($response.result.isError -or $response.result.structuredContent.camera.fov_degrees -ne 52.0 -or
            $response.result.structuredContent.transform.rotation_degrees.y -ne 35.0) {
            throw 'get_scene_state did not observe the camera and transform updates.'
        }

        Send-JsonMessage $process ([ordered]@{
            jsonrpc = '2.0'
            id = 7
            method = 'resources/read'
            params = [ordered]@{ uri = 'graphics://scene/state' }
        })
        $response = Read-JsonResponse $process 7 'resources/read'
        $resourceState = $response.result.contents[0].text | ConvertFrom-Json
        if ($resourceState.camera.fov_degrees -ne 52.0) {
            throw 'graphics://scene/state did not observe the camera update.'
        }
        if ($resourceState.transform.rotation_degrees.y -ne 35.0) {
            throw 'graphics://scene/state did not observe the transform update.'
        }

        Send-JsonMessage $process ([ordered]@{ jsonrpc = '2.0'; id = 8; method = 'unknown/method'; params = @{} })
        $response = Read-JsonResponse $process 8 'unknown method'
        if ($response.error.code -ne -32601) {
            throw 'Unknown method did not return -32601.'
        }

        $process.StandardInput.Close()
        $inputClosed = $true
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            throw "The process did not exit within $TimeoutSeconds seconds after stdin was closed."
        }

        Assert-NoUnexpectedStdout ($process.StandardOutput.ReadToEnd())
        if ($process.ExitCode -ne 0) {
            $stderr = $process.StandardError.ReadToEnd()
            throw "The process exited with code $($process.ExitCode). stderr: $stderr"
        }
        Write-Host "PASS $Name"
    }
    finally {
        if ($started -and -not $inputClosed -and -not $process.HasExited) {
            try { $process.StandardInput.Close() } catch { }
        }
        if ($started -and -not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}

$d3d12 = Resolve-SampleExecutable $D3D12Executable 'SimpleMCPGraphicsSampleD3D12'
$vulkan = Resolve-SampleExecutable $VulkanExecutable 'SimpleMCPGraphicsSampleVulkan'

Invoke-McpSmokeTest 'D3D12' $d3d12
Invoke-McpSmokeTest 'Vulkan' $vulkan
Write-Host 'All stdio MCP smoke tests passed.'
