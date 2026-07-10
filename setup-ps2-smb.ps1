# Configures this PC as an SMB game server for OPL (Open PS2 Loader) on a real PS2.
#
# OPL only speaks SMBv1, which Windows 10 ships disabled - this script enables the
# SMB1 *server* component (client stays off), creates the share and a dedicated
# local user, and opens the firewall for the private network profile.
#
# Idempotent: safe to re-run, e.g. after Windows auto-removes SMB1 following
# ~15 days of the protocol being unused (SMB1Protocol-Deprecation feature).
#
# A reboot is required the first time the SMB1 server component is installed.
#
# OPL settings that match the defaults below:
#   Server IP: this PC's LAN address   Port: 445   Share: PS2SMB
#   User: ps2   Password: ps2games
#   ISOs go into <SharePath>\DVD and <SharePath>\CD.

param(
    [string]$SharePath = 'F:\PS2\SMB',
    [string]$ShareName = 'PS2SMB',
    [string]$SmbUser   = 'ps2',
    [string]$SmbPassword = 'ps2games'
)

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host 'Elevation required - relaunching as administrator...'
    $argList = @('-NoExit', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
                 '-SharePath', "`"$SharePath`"", '-ShareName', $ShareName,
                 '-SmbUser', $SmbUser, '-SmbPassword', $SmbPassword)
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    return
}

$ErrorActionPreference = 'Stop'
$restartNeeded = $false

Write-Host "=== 1/6 Folder structure under $SharePath ==="
foreach ($sub in 'DVD', 'CD', 'VMC', 'CFG', 'ART', 'THM') {
    New-Item -ItemType Directory -Force (Join-Path $SharePath $sub) | Out-Null
}
Write-Host 'OK'

Write-Host '=== 2/6 SMB1 server component (client stays disabled) ==='
$server = Get-WindowsOptionalFeature -Online -FeatureName SMB1Protocol-Server
if ($server.State -ne 'Enabled') {
    $result = Enable-WindowsOptionalFeature -Online -FeatureName SMB1Protocol-Server -All -NoRestart
    if ($result.RestartNeeded) { $restartNeeded = $true }
}
# -All also drags in the SMB1 client via the parent feature; OPL never needs it
$client = Get-WindowsOptionalFeature -Online -FeatureName SMB1Protocol-Client
if ($client.State -eq 'Enabled') {
    Disable-WindowsOptionalFeature -Online -FeatureName SMB1Protocol-Client -NoRestart | Out-Null
}
Write-Host 'OK'

Write-Host '=== 3/6 SMB1 in server configuration ==='
try {
    Set-SmbServerConfiguration -EnableSMB1Protocol $true -Force
    Write-Host 'OK'
} catch {
    # "The specified service does not exist" until the SMB1 driver loads on reboot
    $restartNeeded = $true
    Write-Host 'Deferred - SMB1 driver not loaded yet (reboot first, then re-run this script)'
}

Write-Host "=== 4/6 Local user '$SmbUser' ==="
if (-not (Get-LocalUser -Name $SmbUser -ErrorAction SilentlyContinue)) {
    New-LocalUser -Name $SmbUser -Password (ConvertTo-SecureString $SmbPassword -AsPlainText -Force) `
        -PasswordNeverExpires -AccountNeverExpires -Description 'PS2 OPL SMB access' | Out-Null
}
# keep the account off the Windows login screen
$userList = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon\SpecialAccounts\UserList'
if (-not (Test-Path $userList)) { New-Item -Path $userList -Force | Out-Null }
Set-ItemProperty -Path $userList -Name $SmbUser -Value 0 -Type DWord
Write-Host 'OK'

Write-Host "=== 5/6 Share '$ShareName' ==="
if (-not (Get-SmbShare -Name $ShareName -ErrorAction SilentlyContinue)) {
    # ChangeAccess: OPL writes per-game configs and virtual memory cards to the share
    New-SmbShare -Name $ShareName -Path $SharePath -ChangeAccess $SmbUser | Out-Null
}
icacls $SharePath /grant "${SmbUser}:(OI)(CI)M" | Out-Null
Write-Host 'OK'

Write-Host '=== 6/6 Firewall (File and Printer Sharing, private/domain profiles only) ==='
# language-neutral group id for "File and Printer Sharing"
Get-NetFirewallRule -Group '@FirewallAPI.dll,-28502' |
    Where-Object { $_.Profile -match 'Private|Domain' } |
    Enable-NetFirewallRule
Write-Host 'OK'

Write-Host ''
if ($restartNeeded) {
    Write-Host 'RESTART REQUIRED - reboot Windows, then re-run this script to finish.' -ForegroundColor Yellow
} else {
    $smb1 = (Get-SmbServerConfiguration).EnableSMB1Protocol
    if (-not $smb1) {
        Write-Host 'WARNING: EnableSMB1Protocol is still false - reboot and re-run.' -ForegroundColor Yellow
    } else {
        $ip = (Get-NetConnectionProfile | ForEach-Object {
                  Get-NetIPAddress -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4
              } | Select-Object -First 1).IPAddress
        Write-Host "Done. In OPL set: Server IP $ip, port 445, share $ShareName, user $SmbUser." -ForegroundColor Green
    }
}
