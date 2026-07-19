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

if (-not ('HttpSmokeWindowCloser' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class HttpSmokeWindowCloser
{
    private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    public static bool CloseAll(uint processId)
    {
        bool found = false;
        EnumWindows((window, parameter) =>
        {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId)
            {
                PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero);
                found = true;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
}

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
    return (Resolve-Path -LiteralPath $candidate -ErrorAction Stop).Path
}

function ConvertTo-JsonBody {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Depth 30 -Compress)
}

function Invoke-McpHttp {
    param(
        [Parameter(Mandatory)]
        [string] $Uri,

        [Parameter(Mandatory)]
        [ValidateSet('POST', 'DELETE', 'GET')]
        [string] $Method,

        [hashtable] $Headers = @{},

        [AllowEmptyString()]
        [string] $Body = ''
    )

    $arguments = @{
        Uri = $Uri
        Method = $Method
        Headers = $Headers
        SkipHttpErrorCheck = $true
        TimeoutSec = $TimeoutSeconds
    }
    if ($Method -eq 'POST') {
        $arguments.ContentType = 'application/json'
        $arguments.Body = $Body
    }
    return Invoke-WebRequest @arguments
}

function Wait-Endpoint {
    param([Parameter(Mandatory)] [string] $Endpoint)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $response = Invoke-McpHttp -Uri $Endpoint -Method GET
            if ($response.StatusCode -eq 405) {
                return
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "HTTP endpoint did not become ready: $Endpoint"
}

function Assert-Status {
    param(
        [Parameter(Mandatory)] $Response,
        [Parameter(Mandatory)] [int] $Expected,
        [Parameter(Mandatory)] [string] $Operation
    )
    if ($Response.StatusCode -ne $Expected) {
        throw "$Operation returned HTTP $($Response.StatusCode), expected $Expected. Body: $($Response.Content)"
    }
}

function Close-SampleProcess {
    param([Parameter(Mandatory)] [System.Diagnostics.Process] $Process)

    if ($Process.HasExited) {
        return
    }
    $sent = [HttpSmokeWindowCloser]::CloseAll([uint32] $Process.Id)
    if ($sent -and $Process.WaitForExit($TimeoutSeconds * 1000)) {
        return
    }

    # This is test cleanup for the exact process started above, used only if graceful WM_CLOSE fails.
    Stop-Process -Id $Process.Id -Force
    $Process.WaitForExit()
}

function Open-McpSession {
    param(
        [Parameter(Mandatory)] [string] $Endpoint,
        [Parameter(Mandatory)] [string] $ClientName
    )

    $initializeBody = ConvertTo-JsonBody ([ordered]@{
        jsonrpc = '2.0'
        id = 1
        method = 'initialize'
        params = [ordered]@{
            protocolVersion = '2025-11-25'
            capabilities = @{}
            clientInfo = [ordered]@{ name = $ClientName; version = '1.0' }
        }
    })
    $response = Invoke-McpHttp -Uri $Endpoint -Method POST -Headers @{
        Accept = 'application/json, text/event-stream'
    } -Body $initializeBody
    Assert-Status $response 200 "initialize $ClientName"

    $sessionId = [string] ($response.Headers['MCP-Session-Id'] | Select-Object -First 1)
    $headers = @{
        Accept = 'application/json, text/event-stream'
        'MCP-Session-Id' = $sessionId
        'MCP-Protocol-Version' = '2025-11-25'
    }
    $initializedBody = ConvertTo-JsonBody ([ordered]@{
        jsonrpc = '2.0'; method = 'notifications/initialized'; params = @{}
    })
    $response = Invoke-McpHttp -Uri $Endpoint -Method POST -Headers $headers -Body $initializedBody
    Assert-Status $response 202 "notifications/initialized $ClientName"

    return [pscustomobject]@{
        Id = $sessionId
        Headers = $headers
    }
}

function Invoke-McpTool {
    param(
        [Parameter(Mandatory)] [string] $Endpoint,
        [Parameter(Mandatory)] [hashtable] $Headers,
        [Parameter(Mandatory)] [int] $Id,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [hashtable] $Arguments
    )

    $body = ConvertTo-JsonBody ([ordered]@{
        jsonrpc = '2.0'
        id = $Id
        method = 'tools/call'
        params = [ordered]@{ name = $Name; arguments = $Arguments }
    })
    $response = Invoke-McpHttp -Uri $Endpoint -Method POST -Headers $Headers -Body $body
    Assert-Status $response 200 $Name
    return ($response.Content | ConvertFrom-Json)
}

function Invoke-SimultaneousDefaultPortTest {
    param(
        [Parameter(Mandatory)] [string] $D3D12Path,
        [Parameter(Mandatory)] [string] $VulkanPath
    )

    $samples = @(
        [pscustomobject]@{
            Name = 'HTTP D3D12'
            Executable = $D3D12Path
            Endpoint = 'http://127.0.0.1:5000/mcp'
            Fov = 41.0
            Process = $null
            Session = $null
        },
        [pscustomobject]@{
            Name = 'HTTP Vulkan'
            Executable = $VulkanPath
            Endpoint = 'http://127.0.0.1:5001/mcp'
            Fov = 73.0
            Process = $null
            Session = $null
        }
    )

    Write-Host 'Testing simultaneous default endpoints and independent scene state'
    try {
        foreach ($sample in $samples) {
            $sample.Process = Start-Process `
                -FilePath $sample.Executable `
                -WorkingDirectory ([System.IO.Path]::GetTempPath()) `
                -WindowStyle Hidden `
                -PassThru
        }
        foreach ($sample in $samples) {
            Wait-Endpoint $sample.Endpoint
            $sample.Session = Open-McpSession $sample.Endpoint "simultaneous $($sample.Name)"
            $null = Invoke-McpTool $sample.Endpoint $sample.Session.Headers 2 'set_camera' @{
                fov_degrees = $sample.Fov
            }
        }

        foreach ($sample in $samples) {
            $state = Invoke-McpTool $sample.Endpoint $sample.Session.Headers 3 'get_scene_state' @{}
            if ($state.result.structuredContent.camera.fov_degrees -ne $sample.Fov) {
                throw "$($sample.Name) did not preserve its independent scene state."
            }
            $response = Invoke-McpHttp -Uri $sample.Endpoint -Method DELETE -Headers @{
                'MCP-Session-Id' = $sample.Session.Id
                'MCP-Protocol-Version' = '2025-11-25'
            }
            Assert-Status $response 204 "session DELETE $($sample.Name)"
        }

        Write-Host 'PASS simultaneous default endpoints (D3D12 FOV=41, Vulkan FOV=73)'
    }
    finally {
        foreach ($sample in $samples) {
            if ($null -ne $sample.Process) {
                Close-SampleProcess $sample.Process
                $sample.Process.Dispose()
            }
        }
    }
}

function Invoke-HttpSmokeTest {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Executable,
        [Parameter(Mandatory)] [int] $Port
    )

    $endpoint = "http://127.0.0.1:$Port/mcp"
    Write-Host "Testing $Name`: $Executable at $endpoint"

    $process = Start-Process `
        -FilePath $Executable `
        -ArgumentList '--port', $Port `
        -WorkingDirectory ([System.IO.Path]::GetTempPath()) `
        -WindowStyle Hidden `
        -PassThru

    try {
        Wait-Endpoint $endpoint

        $initializeBody = ConvertTo-JsonBody ([ordered]@{
            jsonrpc = '2.0'
            id = 1
            method = 'initialize'
            params = [ordered]@{
                protocolVersion = '2025-11-25'
                capabilities = @{}
                clientInfo = [ordered]@{ name = 'HTTP smoke test'; version = '1.0' }
            }
        })
        $accept = @{ Accept = 'application/json, text/event-stream' }

        $forbiddenHeaders = @{
            Accept = 'application/json, text/event-stream'
            Origin = 'https://example.invalid'
        }
        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $forbiddenHeaders -Body $initializeBody
        Assert-Status $response 403 'invalid Origin'

        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $accept -Body $initializeBody
        Assert-Status $response 200 'initialize'
        $initialize = $response.Content | ConvertFrom-Json
        if ($initialize.result.protocolVersion -ne '2025-11-25') {
            throw 'initialize returned an unexpected protocol version.'
        }
        $sessionId = [string] ($response.Headers['MCP-Session-Id'] | Select-Object -First 1)
        if ($sessionId.Length -ne 64) {
            throw 'initialize did not return a 256-bit hex MCP-Session-Id.'
        }

        $sessionHeaders = @{
            Accept = 'application/json, text/event-stream'
            'MCP-Session-Id' = $sessionId
            'MCP-Protocol-Version' = '2025-11-25'
        }
        $initializedBody = ConvertTo-JsonBody ([ordered]@{
            jsonrpc = '2.0'; method = 'notifications/initialized'; params = @{}
        })
        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $sessionHeaders -Body $initializedBody
        Assert-Status $response 202 'notifications/initialized'

        $toolsBody = ConvertTo-JsonBody ([ordered]@{
            jsonrpc = '2.0'; id = 2; method = 'tools/list'; params = @{}
        })
        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $sessionHeaders -Body $toolsBody
        Assert-Status $response 200 'tools/list'
        $toolNames = @(($response.Content | ConvertFrom-Json).result.tools | ForEach-Object { $_.name })
        foreach ($required in @('get_scene_state', 'set_camera', 'set_light', 'set_transform', 'reset_scene')) {
            if ($required -notin $toolNames) {
                throw "tools/list did not include $required."
            }
        }

        $setBody = ConvertTo-JsonBody ([ordered]@{
            jsonrpc = '2.0'
            id = 3
            method = 'tools/call'
            params = [ordered]@{ name = 'set_camera'; arguments = [ordered]@{ fov_degrees = 52.0 } }
        })
        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $sessionHeaders -Body $setBody
        Assert-Status $response 200 'set_camera'
        $setResult = $response.Content | ConvertFrom-Json
        if ($setResult.result.isError -or $setResult.result.structuredContent.camera.fov_degrees -ne 52.0) {
            throw 'set_camera did not update the HTTP sample state.'
        }

        $resourceBody = ConvertTo-JsonBody ([ordered]@{
            jsonrpc = '2.0'
            id = 4
            method = 'resources/read'
            params = [ordered]@{ uri = 'graphics://app/info' }
        })
        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $sessionHeaders -Body $resourceBody
        Assert-Status $response 200 'resources/read app info'
        $resourceEnvelope = $response.Content | ConvertFrom-Json
        $appInfo = $resourceEnvelope.result.contents[0].text | ConvertFrom-Json
        if ($appInfo.mcp.transport -ne 'streamable-http' -or $appInfo.mcp.active_sessions -ne 1) {
            throw 'app info did not expose HTTP transport and active session state.'
        }

        $response = Invoke-McpHttp -Uri $endpoint -Method DELETE -Headers @{
            'MCP-Session-Id' = $sessionId
            'MCP-Protocol-Version' = '2025-11-25'
        }
        Assert-Status $response 204 'session DELETE'

        $response = Invoke-McpHttp -Uri $endpoint -Method POST -Headers $sessionHeaders -Body $toolsBody
        Assert-Status $response 404 'request after DELETE'

        Write-Host "PASS $Name"
    }
    finally {
        Close-SampleProcess $process
        $process.Dispose()
    }
}

$d3d12 = Resolve-SampleExecutable $D3D12Executable 'SimpleMCPGraphicsSampleHttpD3D12'
$vulkan = Resolve-SampleExecutable $VulkanExecutable 'SimpleMCPGraphicsSampleHttpVulkan'

Invoke-HttpSmokeTest 'HTTP D3D12' $d3d12 18765
Invoke-HttpSmokeTest 'HTTP Vulkan' $vulkan 18766
Invoke-SimultaneousDefaultPortTest $d3d12 $vulkan
Write-Host 'All Streamable HTTP MCP smoke tests passed.'
