$ErrorActionPreference = 'Stop'

# Only the reserved osvault-tests/ group subtree; independent of the test executable
$filter = 'osvlt/6F737661756C742D74657374732F*'

if (-not ('OSVaultTestCleanup' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class OSVaultTestCleanup
{
    // Only the prefix of CREDENTIALW needed to identify records for deletion
    [StructLayout(LayoutKind.Sequential)]
    public struct Header
    {
        public uint Flags;
        public uint Type;
        public IntPtr TargetName;
    }

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    public static extern bool CredEnumerateW(string filter, uint flags, out uint count, out IntPtr entries);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    public static extern bool CredDeleteW(string target, uint type, uint flags);

    [DllImport("advapi32.dll", ExactSpelling = true)]
    public static extern void CredFree(IntPtr buffer);
}
'@
}

function Get-TestTargets {
    [uint32]$count = 0
    $entries = [IntPtr]::Zero
    if (-not [OSVaultTestCleanup]::CredEnumerateW($filter, 0, [ref]$count, [ref]$entries)) {
        $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($code -eq 1168) { return } # ERROR_NOT_FOUND
        throw [ComponentModel.Win32Exception]::new($code)
    }
    try {
        for ($index = 0; $index -lt $count; ++$index) {
            $pointer = [Runtime.InteropServices.Marshal]::ReadIntPtr($entries, $index * [IntPtr]::Size)
            $entry = [Runtime.InteropServices.Marshal]::PtrToStructure($pointer, [type][OSVaultTestCleanup+Header])
            if ($entry.Type -eq 1) { # CRED_TYPE_GENERIC
                [Runtime.InteropServices.Marshal]::PtrToStringUni($entry.TargetName)
            }
        }
    }
    finally {
        [OSVaultTestCleanup]::CredFree($entries)
    }
}

try {
    $failed = $false
    foreach ($target in Get-TestTargets) {
        try {
            if (-not [OSVaultTestCleanup]::CredDeleteW($target, 1, 0)) {
                $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                if ($code -ne 1168) {
                    [Console]::Error.WriteLine("Cannot remove test credential $target (Win32 $code)")
                    $failed = $true
                }
            }
        }
        catch {
            [Console]::Error.WriteLine("Cannot remove test credential ${target}: $($_.Exception.Message)")
            $failed = $true
        }
    }
    foreach ($target in Get-TestTargets) {
        [Console]::Error.WriteLine("Test credential remains: $target")
        $failed = $true
    }
    if ($failed) { exit 1 }
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
